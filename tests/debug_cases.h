#pragma once

#include <stdint.h>
#include <string>

#include "debugLevel.h"

// Included by the existing native runner after Serial, g_ConfigFlags and
// drainSerial() have been declared. Exercise the registered command pipeline.
void testDebugLevels() {
  const uint8_t otherFlags = 0xe6; // RAW, POLL and every unrelated flag.
  const uint8_t debugMask = 1 | Yokis::PollTraceFlag;
  for (uint8_t level = 0; level <= 2; ++level) {
    uint8_t flags = otherFlags;
    CHECK(Yokis::setDebugLevel(flags, level));
    CHECK(Yokis::debugLevel(flags) == level);
    CHECK((flags & static_cast<uint8_t>(~debugMask)) == otherFlags);
    CHECK((flags & debugMask) == (level == 2 ? 1 : level == 1 ? Yokis::PollTraceFlag : 0));
  }
  CHECK(Yokis::debugLevel(1) == 2); // Preserve the existing DEBUG bit meaning.
  CHECK(Yokis::debugLevel(Yokis::PollTraceFlag) == 1);
  CHECK(Yokis::debugLevel(1 | Yokis::PollTraceFlag) == 2);
  CHECK(Yokis::debugLevel(2) == 0); // RAW chooses formatting, not verbosity.
  uint8_t invalid = otherFlags | 1;
  const uint8_t unchanged = invalid;
  CHECK(!Yokis::setDebugLevel(invalid, 3));
  CHECK(invalid == unchanged);
  CHECK(!Yokis::setDebugLevel(invalid, 255));
  CHECK(invalid == unchanged);

  const uint8_t savedFlags = g_ConfigFlags;
  const std::string savedOutput = Serial.out;
  g_ConfigFlags = otherFlags;
  const auto send = [](const char *command) {
    Serial.out.clear();
    Serial.feed(command);
    drainSerial();
  };
  const auto expectLevel = [&](uint8_t level) {
    CHECK(Yokis::debugLevel(g_ConfigFlags) == level);
    CHECK((g_ConfigFlags & static_cast<uint8_t>(~(1 | Yokis::PollTraceFlag))) == otherFlags);
    const std::string::size_type debug = Serial.out.find("DEBUG");
    CHECK(debug != std::string::npos);
    if (debug != std::string::npos) {
      const std::string::size_type end = Serial.out.find('\n', debug);
      const std::string line = Serial.out.substr(debug, end == std::string::npos ? end : end - debug);
      CHECK(line.find(static_cast<char>('0' + level)) != std::string::npos);
    }
  };
  send("debug 0\n");
  expectLevel(0);
  CHECK((g_ConfigFlags & 2) != 0); // RAW remains selected at debug level zero.
  send("debug 1\n");
  expectLevel(1);
  send("debug 2\n");
  expectLevel(2);
  send("debug 0\n");
  expectLevel(0);
  send("debug\n");
  expectLevel(1);
  send("debug\n");
  expectLevel(2);
  send("debug\n");
  expectLevel(0);
  const char *badCommands[] = {
      "debug unknown\n", "debug 3\n", "debug -1\n", "debug +1\n",
      "debug 1x\n", "debug 4294967296\n", "debug 1 2\n"};
  for (const char *command : badCommands) {
    const uint8_t before = g_ConfigFlags;
    send(command);
    CHECK(g_ConfigFlags == before);
  }
  g_ConfigFlags = savedFlags;
  Serial.out = savedOutput;
}
