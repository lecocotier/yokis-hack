"""Build and run production-code regressions on Windows or Linux."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", help="C++ compiler executable; defaults to CXX or the local portable compiler")
    parser.add_argument("--sanitizers", choices=("auto", "on", "off"), default="auto")
    args = parser.parse_args()
    windows = os.name == "nt"
    local = ROOT / ".tools/w64devkit/bin/g++.exe"
    compiler = args.cxx or os.environ.get("CXX") or (str(local) if windows and local.exists() else shutil.which("g++"))
    if not compiler:
        parser.error("No C++ compiler. Run: python tools/setup_native.py")
    compiler = str(Path(compiler).resolve()) if Path(compiler).exists() else compiler
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    local_python = ROOT / ".tools/python"
    if local_python.exists():
        env["PYTHONPATH"] = str(local_python) + os.pathsep + env.get("PYTHONPATH", "")
    build = ROOT / "tests/build"
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "tests/extract_callbacks.py"], cwd=ROOT, env=env, check=True)
    flags = ["-std=c++11", "-fno-exceptions", "-g", os.environ.get("HOST_OPTIMIZATION", "-O0"),
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-ignored-qualifiers",
             "-DESP8266", "-DMQTT_ENABLED", "-DYOKIS_NATIVE_TEST", "-DMQTT_MAX_PACKET_SIZE=1024",
             '-DYOKIS_HTTP_PASSWORD="native-http-secret-value"', '-DYOKIS_TELNET_PASSWORD="native-console-secret-value"',
             '-DYOKIS_OTA_PASSWORD="native-ota-secret-value"', '-DYOKIS_AP_PASSWORD="native-ap-secret-value"',
             "-Itests/host", "-Iinclude"]
    sanitize = args.sanitizers == "on" or (args.sanitizers == "auto" and not windows)
    if sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        env["ASAN_OPTIONS"] = "detect_leaks=1"
    sources = ["tests/test_regression.cpp", "tests/build/callbacks.cpp", "src/RF/device.cpp", "src/RF/e2bp.cpp",
               "src/RF/configurator.cpp", "src/utils.cpp", "src/postStopPolling.cpp", "src/RF/pairing.cpp",
               "src/RF/copy.cpp", "src/RF/scanner.cpp", "src/RF/irqManager.cpp", "src/commands/callbacks.cpp"]
    sources += sorted(path.relative_to(ROOT).as_posix() for path in (ROOT / "src/serial").glob("*.cpp"))
    sources += ["src/net/wifi.cpp", "src/net/webserver.cpp", "src/net/diagnostics.cpp", "src/net/discoveryCleanup.cpp", "src/net/mqtt.cpp", "src/net/mqttHass.cpp",
                "src/net/mqttConfig.cpp", "src/storage/yokisLittleFS.cpp"]
    binary = build / ("regression.exe" if windows else "regression")
    print("Native compiler:", compiler, flush=True)
    print("Instrumentation:", "ASan + UBSan + leaks" if sanitize else "none (Windows portable GCC)", flush=True)
    subprocess.run([compiler, *flags, *sources, "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
    console = build / ("admin-console.exe" if windows else "admin-console")
    subprocess.run([compiler, *flags, "tests/test_admin_console.cpp", "src/serial/adminConsole.cpp",
                    "-o", str(console)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(console)], cwd=ROOT, env=env, check=True)
    radio = build / ("radio.exe" if windows else "radio")
    radio_flags = [f for f in flags if not f.startswith("-I")]
    radio_flags += ["-DARDUINO", "-DMINIMAL", "-DYOKIS_RADIO_TEST", "-Itests/radio_host", "-Iinclude", "-Itests/host"]
    subprocess.run([compiler, *radio_flags, "tests/test_radio_transport.cpp", "src/RF/RF24_forked.cpp",
                    "src/RF/irqTransport.cpp", "-o", str(radio)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(radio)], cwd=ROOT, env=env, check=True)
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py"], cwd=ROOT, env=env, check=True)
    subprocess.run([sys.executable, "tests/check_mqtt_json.py"], cwd=ROOT, env=env, check=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
