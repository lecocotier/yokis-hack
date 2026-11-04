#ifndef YOKIS_ADMIN_CONSOLE_H
#define YOKIS_ADMIN_CONSOLE_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "adminConfig.h"

// USB remains available even when network administration is disabled.
class AdminConsole : public Print {
public:
    explicit AdminConsole(const char* password = YOKIS_TELNET_PASSWORD,
                          uint16_t port = 23);
    void begin(unsigned long baud);
    void handle();
    int available();
    int read();
    bool inputPending() const;
    size_t write(uint8_t byte) override;
    using Print::write;
    bool authenticated() const;
    void setCallbackOnConnect(void (*callback)());
    void setCallbackOnDisconnect(void (*callback)());

private:
    enum {
        CommandCapacity = 255,
        OverflowReportLength = 256,
        InputBudget = 64
    };
    enum Source { NoSource, UsbSource, TcpSource };
    enum TelnetState { TelnetData, TelnetIac, TelnetOption,
                       TelnetSubnegotiation, TelnetSubnegotiationIac };

    struct Line {
        uint8_t bytes[CommandCapacity + 1];
        size_t length;
        size_t position;
        uint32_t lastByteAt;
        bool ready;
        bool synthetic;
        bool discarding;
        bool reportOverflow;
        bool skipLf;
    };

    WiFiServer server_;
    WiFiClient client_;
    char password_[64];
    char authInput_[64];
    size_t passwordLength_;
    size_t authLength_;
    bool configured_;
    bool listening_;
    bool clientPresent_;
    bool authenticated_;
    bool coolingDown_;
    uint32_t acceptedAt_;
    uint32_t failedAt_;
    TelnetState telnetState_;
    Line usb_;
    Line tcp_;
    Source activeSource_;
    Source nextSource_;
    const char* controlOutput_;
    size_t controlPosition_;
    bool outputTruncated_;
    size_t noticePosition_;
    void (*connectCallback_)();
    void (*disconnectCallback_)();

    static void initializeLine(Line& line);
    static bool elapsed(uint32_t now, uint32_t since, uint32_t interval);
    static bool pending(const Line& line);
    static void expireLine(Line& line, uint32_t now);
    static void ingest(Line& line, uint8_t byte, uint32_t now);
    void selectSource();
    void disconnect(bool failed, uint32_t now);
    void acceptClient(uint32_t now);
    bool telnetData(uint8_t byte);
    void ingestAuthentication(uint8_t byte, uint32_t now);
    bool passwordMatches() const;
    void clearAuthenticationInput();
    void pumpOutput();
};

#endif
