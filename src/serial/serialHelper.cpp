#include "serial/serialHelper.h"
#include "serial/displayConfigCallback.h"
#include "serial/flagCallback.h"
#include "serial/usageCallback.h"

SerialHelper::SerialHelper() {
    LOG.begin(SERIAL_BAUDRATE);
    callbacksIndex = 0;
    longestCommandLength = 0;
    currentCommandIdx = 0;
    currentCommand[0] = 0;
    discardLine = previousCR = false;

    this->registerCallback(
        new UsageCallback("help", "display this help", this));
    this->registerCallback(
        new FlagCallback("debug", "toggle debug mode", FLAG_DEBUG));
    this->registerCallback(
        new FlagCallback("raw", "toggle raw / formatted output", FLAG_RAW));
    this->registerCallback(
        new FlagCallback("poll", "toggle devices polling for status", FLAG_POLLING));
    this->registerCallback(
        new DisplayConfigCallback("config", "print current config flags"));
}

SerialHelper::~SerialHelper() {
    for (uint8_t i = 0; i < callbacksIndex; ++i) delete callbacks[i];
}
bool SerialHelper::registerCallback(SerialCallback* callback) {
    if (!callback) return false;
    if (callbacksIndex >= MAX_NUMBER_OF_COMMANDS) { delete callback; return false; }
    callbacks[callbacksIndex++] = callback;
    if (strlen(callback->getCommand()) > longestCommandLength)
        longestCommandLength = strlen(callback->getCommand());
    return true;
}
void SerialHelper::readFromSerial() {
    // Bounded amount of input per loop, with both LF and CRLF supported.
    for (unsigned budget = 0; budget < 32 && LOG.available(); ++budget) {
        char c = char(LOG.read());
        if (c == '\n' && previousCR) { previousCR = false; continue; }
        previousCR = c == '\r';
        if (c == '\r' || c == '\n') {
            LOG.println();
            if (discardLine) LOG.println("Command too long. Aborted entire line.");
            else if (currentCommandIdx) {
                currentCommand[currentCommandIdx] = 0;
                char cmd[MAX_COMMAND_LENGTH + 1]; extractCommand(cmd, sizeof(cmd));
                if (cmd[0] && !executeCallback(cmd)) {
                    LOG.print(cmd); LOG.println(": command not found");
                }
            }
            currentCommandIdx = 0; currentCommand[0] = 0; discardLine = false;
            prompt(); continue;
        }
        if (discardLine) continue;
        if (c == 8 || c == 127) {
            if (currentCommandIdx) { --currentCommandIdx; LOG.print("\b \b"); }
        } else if (static_cast<unsigned char>(c) >= 32 || c == '\t') {
            if (currentCommandIdx + 1 >= MAX_COMMAND_FULL_LENGTH) discardLine = true;
            else { currentCommand[currentCommandIdx++] = c; LOG.print(c); }
        }
    }
}
bool SerialHelper::executeCallback(const char* cmd) {
    if (!cmd || !*cmd) return false;
    for (uint8_t i = 0; i < callbacksIndex; ++i) {
        if (strcmp(cmd, callbacks[i]->getCommand()) == 0) {
            if (!callbacks[i]->commandCallback(currentCommand)) LOG.println("Command failed");
            return true;
        }
    }
    return false;
}
void SerialHelper::extractCommand(char* buf, size_t size) {
    if (!buf || !size) return;
    size_t p = 0;
    while (p < size_t(currentCommandIdx) && currentCommand[p] != ' ' &&
           currentCommand[p] != '\t' && p < MAX_COMMAND_LENGTH && p + 1 < size) {
        buf[p] = currentCommand[p]; ++p;
    }
    buf[p] = 0;
    // Do not execute a truncated long token as an existing shorter command.
    if (p < size_t(currentCommandIdx) && currentCommand[p] != ' ' && currentCommand[p] != '\t') buf[0] = 0;
}

void SerialHelper::usage() {
    char buf[64];
    snprintf(buf, sizeof(buf), PROG_TITLE_FORMAT, PROG_VERSION);
    LOG.println(buf);
    LOG.println();

    for (uint8_t i = 0; i < callbacksIndex; i++) {
        LOG.print(callbacks[i]->getCommand());
        unsigned int nbSpaces =
            longestCommandLength - strlen(callbacks[i]->getCommand());
        for (uint8_t j = 0; j < nbSpaces + 1; j++) LOG.print(" ");
        LOG.println(callbacks[i]->getHelp());
    }
}

bool SerialHelper::displayConfig(void) {
    LOG.print("Config:");
    LOG.print(" DEBUG=");
    LOG.print(IS_DEBUG_ENABLED);
    LOG.print(" RAW=");
    LOG.print(FLAG_IS_ENABLED(FLAG_RAW));
    LOG.print(" POLLING=");
    LOG.print(FLAG_IS_ENABLED(FLAG_POLLING));
    LOG.println();
    return true;
}

void SerialHelper::prompt() {
    LOG.print("> ");
}
