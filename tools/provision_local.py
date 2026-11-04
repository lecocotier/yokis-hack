"""Create independent local administration secrets without displaying them."""
from __future__ import annotations

import configparser
from pathlib import Path
import re
import secrets

ROOT = Path(__file__).resolve().parents[1]


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
    section = "env:d1_mini_ota"
    if not config.has_section(section):
        config.add_section(section)
    flags = config.get(section, "upload_flags", fallback="")
    if not re.search(r"(?:--auth(?:=|\s)|-a(?:=|\s))", flags):
        config.set(section, "upload_flags", (flags + "\n--auth=" + match[1]).strip())
        with path.open("w", encoding="utf-8") as output:
            config.write(output)
    print("Private administration credentials ready in include/local_secrets.h; values not displayed.")
    print("Existing credentials are kept on subsequent runs. Local OTA flags are configured.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
