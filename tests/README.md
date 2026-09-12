# Native reliability regression tests

Run `bash tests/run_native.sh` from any directory on Linux with Python 3 and g++.
The suite uses C++11, AddressSanitizer, UndefinedBehaviorSanitizer and leak checking.
Warnings in the tested application code are errors (platform-irrelevant qualifiers
and unused fixture arguments are explicitly excluded).

`host/` substitutes Arduino time, filesystem, MQTT, WiFi, timers and the RF24
hardware interface. Application implementations in `src/` are compiled directly.
`extract_callbacks.py` copies the three complete production functions from
`src/main.cpp` into a generated translation unit; it does not reimplement them.

The tests cover accepted/rejected command paths, exact shutter payload bytes,
consecutive commands and stops, polling versus command timestamps, failed and
undecodable radio responses, borrowed object lifetimes, state transitions,
rollover, configuration validation/atomic failure, subscription/discovery retry,
partial HTTP changes, CLI bounds and reloading configuration. The simulated radio
checks software control flow, not electrical timing, on-air reception or IRAM.

`tests/build` is generated and excluded from version control. Firmware compilation
is a separate GitHub Actions step for ESP8266 and Arduino Mega. Neither test suite
operates an actual shutter or uploads a firmware.

Post-STOP integration scenarios are in `poststop_cases.h`: due times, bounded
retry, priority over periodic polls, queued MQTT STOPs, console STOP, independent
destinations, RF initialization failures, special RF modes and rollover.
The MQTT fixture consumes one packet per loop; it is not a live broker test.

Network addition: the production Wi-Fi module is now compiled too. Run
`bash tests/run_native.sh` (host default -O0; HOST_OPTIMIZATION can override),
then `python3 tests/check_mqtt_json.py`. Network interfaces remain simulated;
the declared will is checked at the PubSubClient API, not on a real broker.
See `doc/network-availability-2026-09-12.md` for target commissioning.
