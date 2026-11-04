"""Reject private deployment files/known local values in the Git index.

Values are never printed. Historical findings are a separate audit: this check
protects the next commit without silently rewriting published history.
"""
from __future__ import annotations

import argparse
import configparser
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {"platformio.local.ini", "include/local_secrets.h", "DETAILS LIVRAISON.txt"}


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def local_values() -> dict[str, str]:
    values: dict[str, str] = {}
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / "platformio.local.ini", encoding="utf-8")
    for section in config.sections():
        for key, value in config.items(section):
            if key == "upload_port" and value.strip():
                values["ota_destination"] = value.strip()
    notes = ROOT / "DETAILS LIVRAISON.txt"
    if notes.exists():
        text = notes.read_text(encoding="utf-8-sig")
        for label, kind in (("Password", "mqtt_password"), ("Host", "mqtt_host")):
            match = re.search(r"^" + label + r":\s*(.+)$", text, re.M)
            if match and len(match[1].strip()) >= 6:
                values[kind] = match[1].strip()
    header = ROOT / "include/local_secrets.h"
    if header.exists():
        for name, value in re.findall(r'^#define\s+(\w+)\s+"([^"\r\n]+)"', header.read_text(), re.M):
            if len(value) >= 8:
                values[name] = value
    return values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staged", action="store_true", help="Inspect index contents, not working files")
    args = parser.parse_args()
    findings = []
    values = local_values()
    for raw in git("ls-files", "-z").split(b"\0"):
        if not raw:
            continue
        name = raw.decode("utf-8")
        if name in FORBIDDEN or name.startswith(("local/", ".tools/", ".pio/")):
            findings.append({"path": name, "kind": "private_file_tracked"})
            continue
        if args.staged:
            data = git("show", ":" + name)
        else:
            path = ROOT / name
            if not path.is_file():
                continue
            data = path.read_bytes()
        for kind, value in values.items():
            if value.encode("utf-8") in data:
                findings.append({"path": name, "kind": kind})
    if findings:
        print(json.dumps({"private_data_findings": findings}, indent=2))
        return 1
    print("Private configuration check: OK (no values disclosed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
