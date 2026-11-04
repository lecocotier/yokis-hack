#include "serial/debugCallback.h"

#include "debugLevel.h"
#include "globals.h"
#include "reliability.h"
#include "serial/serialHelper.h"

DebugCallback::DebugCallback(const char *command, const char *help)
    : SerialCallback(command, help) {}

bool DebugCallback::commandCallback(const char *params) {
  const Yokis::Arguments arguments(params);
  if (!arguments.valid || arguments.count < 1 || arguments.count > 2) {
    return false;
  }

  uint8_t level;
  if (arguments.count == 1) {
    level = static_cast<uint8_t>((Yokis::debugLevel(g_ConfigFlags) + 1) % 3);
  } else {
    const char *text = arguments.at(1);
    if (text == nullptr || *text == '\0') {
      return false;
    }
    // Require decimal digits explicitly: signs and partial conversions must
    // never change the current flags, regardless of the parser's base support.
    for (const char *digit = text; *digit != '\0'; ++digit) {
      if (*digit < '0' || *digit > '9') {
        return false;
      }
    }
    uint32_t value = 0;
    if (!Yokis::unsignedNumber(text, 2, value)) {
      return false;
    }
    level = static_cast<uint8_t>(value);
  }

  if (!Yokis::setDebugLevel(g_ConfigFlags, level)) {
    return false;
  }
  return SerialHelper::displayConfig();
}
