#pragma once

#include "serial/serialCallback.h"

class DebugCallback : public SerialCallback {
public:
  DebugCallback(const char *command, const char *help);
  bool commandCallback(const char *params) override;
};
