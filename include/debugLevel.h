#pragma once

#include <stdint.h>

namespace Yokis {

static const uint8_t PollTraceFlag = 8;

// Levels: 0 = no automatic poll traces, 1 = poll traces, 2 = detailed debug.
// Both helpers operate on the caller's byte and never access global state.
inline uint8_t debugLevel(uint8_t flags) {
  return (flags & 1) != 0 ? 2 : (flags & PollTraceFlag) != 0 ? 1 : 0;
}

inline bool setDebugLevel(uint8_t &flags, uint8_t level) {
  if (level > 2) {
    return false;
  }
  const uint8_t debugMask = 1 | PollTraceFlag;
  flags &= static_cast<uint8_t>(~debugMask);
  if (level == 2) {
    flags |= 1; // Preserve the existing detailed DEBUG flag.
  } else if (level == 1) {
    flags |= PollTraceFlag;
  }
  return true;
}

} // namespace Yokis
