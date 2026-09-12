# Network availability implementation plan

**Goal:** consolidate the user-validated firmware on master, then improve network recovery on an isolated branch.
**Architecture:** preserve the synchronous RF commands, per-shutter context and post-STOP scheduler. MQTT owns a stable gateway identity and retained will; MqttHass owns incremental discovery/state replay and the Home Assistant birth subscription. Wi-Fi persistence is enabled only for deliberate credential changes.
**Stack:** ESP8266 Arduino 3.1.2, PubSubClient 2.8, PlatformIO espressif8266 4.2.1; existing native production-code harness.
**Approved scope:** conversation of 2026-09-12: exact RF availability, gateway LWT, selective upstream Wi-Fi fixes, refresh on reconnect/HA restart. No RF/authentication/storage-format changes. New fixes remain on their own branch for hardware testing.

## Tasks
- [x] Re-run baseline native/JSON tests, verify CI and ancestor relationship, preserve reference branch, fast-forward fork master to 091721a.
- [x] Add failing tests using production functions for Offline rediscovery, stable CONNECT/will, clean disconnect, failed Online publication, birth-message handling/coalescing, progressive state replay and command/post-STOP priority.
- [x] Modify include/net/mqtt.h and src/net/mqtt.cpp: stable gateway identity/topic, retained Offline will, explicit Online retry, graceful Offline, Wi-Fi gate and finite MQTT response timeout; keep clean sessions and configured topics.
- [x] Modify include/net/mqttHass.h and src/net/mqttHass.cpp: two availability topics/all, known per-device availability, bounded discovery and delayed state replay without mutating RF state or its timestamp, exact HA birth handling, bounded JSON.
- [x] Modify src/main.cpp and src/commands/callbacks.cpp to service deferred refresh outside callback/IRQ, preserve pending-command and post-STOP priority, and send Offline before orderly restart/OTA/configuration changes.
- [x] Add native Wi-Fi substitution exposing persistence effects; compile src/net/wifi.cpp itself. Reproduce and fix identical-credentials reconnect, persistence scope, preserving saved credentials during reconnect/AP fallback, recoverable failed connection. Avoid automatic credential erase.
- [x] Re-run all native tests with ASan/UBSan/leaks plus generated MQTT JSON/Jinja validation. Add failure/retry, maxlength and rollover scenarios.
- [x] Update branch CI for all supported builds and native network simulations; document provenance and hardware validation steps. Verify changed Git tree exactly matches tested content; publish commits on network branch only.
- [x] Inspect CI tests/builds and artifacts, verify master/reference unchanged and report new branch/commit/build version honestly (final gate after push).

## Verification commands
```sh
bash tests/run_native.sh
python3 tests/check_mqtt_json.py
pio run -e d1_mini -e megaatmega2560
pio run -e d1_mini_ota --upload-port 127.0.0.1
```
No upload target, filesystem image or erase command is invoked by automation.
