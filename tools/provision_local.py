"""Create independent local administration secrets without displaying them."""
from __future__ import annotations

import configparser
from pathlib import Path
import re
import secrets

ROOT = Path(__file__).resolve().parents[1]


def isolate_ota_flags(config: configparser.ConfigParser) -> tuple[str, bool]:
    """Keep secrets outside the environment printed by PlatformIO --verbose."""
    environment = "env:d1_mini_ota"
    private = "ota_private:d1_mini_ota"
    changed = False
    if not config.has_section(environment):
        config.add_section(environment)
        changed = True
    legacy = config.get(environment, "upload_flags", fallback="")
    current = config.get(private, "upload_flags", fallback="")
    if legacy and current and legacy != current:
        raise RuntimeError("Conflicting OTA flags; review both local sections privately")
    if not config.has_section(private):
        config.add_section(private)
        changed = True
    if legacy and not current:
        config.set(private, "upload_flags", legacy)
        changed = True
    if config.remove_option(environment, "upload_flags"):
        changed = True
    return private, changed


def main() -> int:
    header = ROOT / "include/local_secrets.h"
    if not header.exists():
        values = {key: secrets.token_hex(16) for key in ("HTTP", "TELNET", "OTA", "AP")}
        content = "// PRIVATE installation credentials. This file is ignored by Git.\n#pragma once\n"
        content += '#define YOKIS_HTTP_USER "admin"\n'
        content += "".join(f'#define YOKIS_{key}_PASSWORD "{value}"\n' for key, value in values.items())
        with header.open("x", encoding="utf-8") as output:
            output.write(content)
    content = header.read_text(encoding="utf-8")
    match = re.search(r'^#define YOKIS_OTA_PASSWORD "([0-9a-f]{32})"$', content, re.M)
    if not match:
        raise RuntimeError("Existing local header retained; configure its OTA upload credentials manually")
    path = ROOT / "platformio.local.ini"
    config = configparser.ConfigParser(interpolation=None)
    config.read(path, encoding="utf-8")
    section, changed = isolate_ota_flags(config)
    flags = config.get(section, "upload_flags", fallback="")
    if not re.search(r"(?:^|\s)(?:--auth(?:=|\s)|-a)", flags):
        config.set(section, "upload_flags", (flags + "\n--auth=" + match[1]).strip())
        changed = True
    if changed:
        with path.open("w", encoding="utf-8") as output:
            config.write(output)
    print("Private administration credentials ready in include/local_secrets.h; values not displayed.")
    print("Existing credentials are kept. OTA flags are isolated from PlatformIO environment output.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
