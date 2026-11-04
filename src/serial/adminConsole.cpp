#include "serial/adminConsole.h"

#include <cstring>

namespace {
const uint32_t AuthenticationTimeoutMs = 30000;
const uint32_t AuthenticationCooldownMs = 2000;
const uint32_t PartialInputTimeoutMs = 5000;
const char PasswordPrompt[] = "\xff\xfb\x01\r\nPassword: ";
const char AuthenticatedNotice[] = "\xff\xfc\x01\r\nAuthenticated.\r\n";
const char TruncationNotice[] =
    "\r\n[Console output truncated; retry via USB.]\r\n";
}

AdminConsole::AdminConsole(const char* password, uint16_t port)
    : server_(port), client_(), password_{0}, authInput_{0},
      passwordLength_(0), authLength_(0), configured_(false),
      listening_(false), clientPresent_(false), authenticated_(false),
      coolingDown_(false), acceptedAt_(0), failedAt_(0),
      telnetState_(TelnetData), usb_(), tcp_(), activeSource_(NoSource),
      nextSource_(UsbSource), controlOutput_(nullptr), controlPosition_(0),
      outputTruncated_(false), noticePosition_(0), connectCallback_(nullptr),
      disconnectCallback_(nullptr) {
    initializeLine(usb_);
    initializeLine(tcp_);
    if (!password) return;
    size_t length = 0;
    while (length < sizeof(password_) && password[length]) ++length;
    if (length < 8 || length >= sizeof(password_) ||
        !Yokis::validAdminSecret(password)) return;
    std::memcpy(password_, password, length);
    passwordLength_ = length;
    configured_ = true;
}

void AdminConsole::begin(unsigned long baud) {
    Serial.begin(baud);
    if (!configured_ || listening_) return;
    server_.setNoDelay(true);
    server_.begin();
    listening_ = true;
}

bool AdminConsole::elapsed(uint32_t now, uint32_t since, uint32_t interval) {
    return static_cast<uint32_t>(now - since) >= interval;
}

void AdminConsole::initializeLine(Line& line) {
    line = Line{};
}

bool AdminConsole::pending(const Line& line) {
    return line.ready || (!line.discarding && line.length != 0);
}

void AdminConsole::expireLine(Line& line, uint32_t now) {
    if (line.ready || line.discarding || line.length == 0 ||
        !elapsed(now, line.lastByteAt, PartialInputTimeoutMs)) return;
    line.length = 0;
    line.position = 0;
    line.discarding = true;
    line.reportOverflow = false;
    line.synthetic = false;
}

void AdminConsole::ingest(Line& line, uint8_t byte, uint32_t now) {
    if (line.ready) return;
    if (line.skipLf) {
        line.skipLf = false;
        if (byte == '\n') return;
    }
    if (byte == '\r' || byte == '\n') {
        line.skipLf = byte == '\r';
        if (line.discarding) {
            const bool report = line.reportOverflow;
            line.discarding = false;
            line.reportOverflow = false;
            line.position = 0;
            line.length = 0;
            if (report) {
                // SerialHelper receives a whole rejected line, never its tail.
                line.synthetic = true;
                line.length = OverflowReportLength + 1;
                line.ready = true;
            }
            return;
        }
        line.bytes[line.length++] = '\n';
        line.position = 0;
        line.synthetic = false;
        line.ready = true;
        return;
    }
    line.lastByteAt = now;
    if (line.discarding) return;
    if (line.length == CommandCapacity) {
        line.length = 0;
        line.position = 0;
        line.discarding = true;
        line.reportOverflow = true;
        return;
    }
    line.bytes[line.length++] = byte;
}

void AdminConsole::handle() {
    const uint32_t now = static_cast<uint32_t>(millis());
    expireLine(usb_, now);
    expireLine(tcp_, now);

    // Each source has an independent budget and retains its own partial line.
    for (unsigned i = 0; i < InputBudget && !usb_.ready && Serial.available(); ++i) {
        const int byte = Serial.read();
        if (byte < 0) break;
        ingest(usb_, static_cast<uint8_t>(byte), now);
    }

    if (!listening_) return;
    if (clientPresent_ && !client_.connected()) disconnect(false, now);
    if (server_.hasClient()) acceptClient(now);
    if (!clientPresent_) return;
    if (!authenticated_ && elapsed(now, acceptedAt_, AuthenticationTimeoutMs)) {
        disconnect(true, now);
        return;
    }

    pumpOutput();
    for (unsigned i = 0; i < InputBudget && clientPresent_ &&
         client_.connected() && client_.available(); ++i) {
        if (authenticated_ && tcp_.ready) break;
        const int input = client_.read();
        if (input < 0) break;
        const uint8_t byte = static_cast<uint8_t>(input);
        if (!telnetData(byte)) continue;
        if (authenticated_) ingest(tcp_, byte, now);
        else ingestAuthentication(byte, now);
    }
    pumpOutput();
}

void AdminConsole::acceptClient(uint32_t now) {
    WiFiClient incoming = server_.accept();
    if (!incoming) return;
    if (coolingDown_ && elapsed(now, failedAt_, AuthenticationCooldownMs)) {
        coolingDown_ = false;
    }
    if (clientPresent_ || coolingDown_) {
        incoming.stop();
        return;
    }
    client_ = incoming;
    client_.setNoDelay(true);
    clientPresent_ = true;
    authenticated_ = false;
    acceptedAt_ = now;
    telnetState_ = TelnetData;
    clearAuthenticationInput();
    outputTruncated_ = false;
    noticePosition_ = 0;
    controlOutput_ = PasswordPrompt;
    controlPosition_ = 0;
}

void AdminConsole::disconnect(bool failed, uint32_t now) {
    const bool notify = authenticated_;
    client_.stop();
    clientPresent_ = false;
    authenticated_ = false;
    clearAuthenticationInput();
    telnetState_ = TelnetData;
    controlOutput_ = nullptr;
    controlPosition_ = 0;
    outputTruncated_ = false;
    noticePosition_ = 0;
    // Completed accepted lines may finish. Only this session's partial line dies.
    if (!tcp_.ready) {
        initializeLine(tcp_);
        if (activeSource_ == TcpSource) activeSource_ = NoSource;
    }
    if (failed) {
        failedAt_ = now;
        coolingDown_ = true;
    }
    if (notify && disconnectCallback_) disconnectCallback_();
}

bool AdminConsole::telnetData(uint8_t byte) {
    switch (telnetState_) {
    case TelnetData:
        if (byte == 255) {
            telnetState_ = TelnetIac;
            return false;
        }
        return true;
    case TelnetIac:
        if (byte == 251 || byte == 252 || byte == 253 || byte == 254)
            telnetState_ = TelnetOption;
        else if (byte == 250) telnetState_ = TelnetSubnegotiation;
        else telnetState_ = TelnetData;
        // Includes IAC IP and other terminal controls: never application commands.
        return false;
    case TelnetOption:
        telnetState_ = TelnetData;
        return false;
    case TelnetSubnegotiation:
        if (byte == 255) telnetState_ = TelnetSubnegotiationIac;
        return false;
    case TelnetSubnegotiationIac:
        telnetState_ = byte == 240 ? TelnetData : TelnetSubnegotiation;
        return false;
    }
    return false;
}

void AdminConsole::clearAuthenticationInput() {
    // Do not retain a submitted credential after success or failure.
    volatile char* buffer = authInput_;
    for (size_t i = 0; i < sizeof(authInput_); ++i) buffer[i] = 0;
    authLength_ = 0;
}

bool AdminConsole::passwordMatches() const {
    uint8_t difference = static_cast<uint8_t>(authLength_ ^ passwordLength_);
    for (size_t i = 0; i < sizeof(password_) - 1; ++i) {
        difference |= static_cast<uint8_t>(authInput_[i]) ^
                      static_cast<uint8_t>(password_[i]);
    }
    return difference == 0;
}

void AdminConsole::ingestAuthentication(uint8_t byte, uint32_t now) {
    if (byte == '\r' || byte == '\n') {
        if (!passwordMatches()) {
            disconnect(true, now);
            return;
        }
        authenticated_ = true;
        clearAuthenticationInput();
        tcp_.skipLf = byte == '\r';
        controlOutput_ = AuthenticatedNotice;
        controlPosition_ = 0;
        pumpOutput();
        if (connectCallback_) connectCallback_();
        return;
    }
    if (authLength_ == sizeof(authInput_) - 1) {
        disconnect(true, now);
        return;
    }
    authInput_[authLength_++] = static_cast<char>(byte);
}

void AdminConsole::pumpOutput() {
    if (!clientPresent_ || !client_.connected()) return;
    size_t budget = InputBudget;
    if (controlOutput_) {
        const int capacity = client_.availableForWrite();
        if (capacity <= 0) return;
        size_t count = std::strlen(controlOutput_ + controlPosition_);
        if (count > static_cast<size_t>(capacity)) count = capacity;
        if (count > budget) count = budget;
        size_t written = client_.write(
            reinterpret_cast<const uint8_t*>(controlOutput_ + controlPosition_), count);
        if (written > count) written = count;
        controlPosition_ += written;
        budget -= written;
        if (controlOutput_[controlPosition_]) return;
        controlOutput_ = nullptr;
        controlPosition_ = 0;
    }
    if (!authenticated_ || !outputTruncated_ || budget == 0) return;
    const int capacity = client_.availableForWrite();
    if (capacity <= 0) return;
    size_t count = sizeof(TruncationNotice) - 1 - noticePosition_;
    if (count > static_cast<size_t>(capacity)) count = capacity;
    if (count > budget) count = budget;
    size_t written = client_.write(
        reinterpret_cast<const uint8_t*>(TruncationNotice + noticePosition_), count);
    if (written > count) written = count;
    noticePosition_ += written;
    if (noticePosition_ == sizeof(TruncationNotice) - 1) {
        outputTruncated_ = false;
        noticePosition_ = 0;
    }
}

size_t AdminConsole::write(uint8_t byte) {
    const size_t written = Serial.write(byte);
    if (authenticated_ && clientPresent_ && client_.connected()) {
        if (controlOutput_ || outputTruncated_ || client_.availableForWrite() <= 0 ||
            client_.write(byte) != 1) outputTruncated_ = true;
    }
    return written;
}

void AdminConsole::selectSource() {
    if (activeSource_ != NoSource) return;
    if (nextSource_ == UsbSource) {
        if (usb_.ready) activeSource_ = UsbSource;
        else if (tcp_.ready) activeSource_ = TcpSource;
    } else {
        if (tcp_.ready) activeSource_ = TcpSource;
        else if (usb_.ready) activeSource_ = UsbSource;
    }
    if (activeSource_ != NoSource)
        nextSource_ = activeSource_ == UsbSource ? TcpSource : UsbSource;
}

int AdminConsole::available() {
    selectSource();
    if (activeSource_ == NoSource) return 0;
    const Line& line = activeSource_ == UsbSource ? usb_ : tcp_;
    return static_cast<int>(line.length - line.position);
}

int AdminConsole::read() {
    if (!available()) return -1;
    Line& line = activeSource_ == UsbSource ? usb_ : tcp_;
    const int byte = line.synthetic
        ? (line.position < OverflowReportLength ? 'X' : '\n')
        : line.bytes[line.position];
    ++line.position;
    if (line.position == line.length) {
        const bool skipLf = line.skipLf;
        initializeLine(line);
        line.skipLf = skipLf;
        activeSource_ = NoSource;
    }
    return byte;
}

bool AdminConsole::inputPending() const {
    return pending(usb_) || pending(tcp_);
}

bool AdminConsole::authenticated() const {
    // WiFiClient::connected() is non-const in the ESP8266 Client interface.
    // handle() observes peer closure; write() independently checks the socket.
    return authenticated_ && clientPresent_;
}

void AdminConsole::setCallbackOnConnect(void (*callback)()) {
    connectCallback_ = callback;
}

void AdminConsole::setCallbackOnDisconnect(void (*callback)()) {
    disconnectCallback_ = callback;
}
