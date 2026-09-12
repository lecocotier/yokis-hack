#!/usr/bin/env bash
set -euo pipefail
export TERM=${TERM:-dumb}
cd "$(dirname "$0")/.."
mkdir -p tests/build
python3 tests/extract_callbacks.py
"${CXX:-g++}" -std=c++11 -fno-exceptions -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-ignored-qualifiers \
  -DESP8266 -DMQTT_ENABLED -DMQTT_MAX_PACKET_SIZE=1024 -Itests/host -Iinclude \
  -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
  tests/test_regression.cpp tests/build/callbacks.cpp \
  src/RF/device.cpp src/RF/e2bp.cpp src/RF/configurator.cpp src/utils.cpp \
  src/postStopPolling.cpp src/RF/pairing.cpp src/RF/copy.cpp src/RF/scanner.cpp src/commands/callbacks.cpp src/serial/*.cpp \
  src/net/webserver.cpp src/net/mqtt.cpp src/net/mqttHass.cpp src/net/mqttConfig.cpp src/storage/yokisLittleFS.cpp \
  -o tests/build/regression
ASAN_OPTIONS=detect_leaks=1 tests/build/regression
