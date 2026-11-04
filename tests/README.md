# Native reliability regression tests

Windows (Python 3, no Bash/WSL required):

```powershell
python tools/setup_native.py
python tests/run_native.py
# Or: ./tests/run_native.ps1
```

The pinned [w64devkit release](https://github.com/skeeto/w64devkit/releases/tag/v2.11.0)
is downloaded from its official project and verified by SHA-256. It and Python
dependencies remain in ignored `.tools/`; no system installation or persistent
PATH change is made. Windows runs the real production-code checks without
sanitizers. Linux/CI retains ASan, UBSan and leak checking. The common runner
also validates the generated JSON/Jinja templates.

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
without a separate JSON invocation (the common runner includes it). Network interfaces remain simulated;
the declared will is checked at the PubSubClient API, not on a real broker.
See `doc/network-availability-2026-09-12.md` for target commissioning.

Audit regressions also cover failure recovery, cooperative dimmer holds,
authenticated HTTP POST/CSRF, preservation/explicit clearing of passwords,
the authenticated USB/TCP console, and pairing IRQ dispatch. A second binary
compiles the real legacy RF24 driver plus the IRQ transport against a simulated
SPI peripheral; it checks interrupt-state restoration and exact command bytes.
Python suites check source/binary receipts and a conservative linked-ELF IRQ
analyzer. CI applies that analyzer to the actual two ESP8266 firmware ELFs.
Its report lists runtime-pointer and hardware/timing limits explicitly.
