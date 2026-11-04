"""Provision local-only native test tools; no system or PATH modifications."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import subprocess
import sys
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
VERSION = "2.11.0"
ARCHIVE = f"w64devkit-x64-{VERSION}.7z.exe"
SHA256 = "54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b"
URL = f"https://github.com/skeeto/w64devkit/releases/download/v{VERSION}/{ARCHIVE}"


def main() -> int:
    tools = ROOT / ".tools"
    tools.mkdir(exist_ok=True)
    if os.name == "nt":
        archive = tools / ARCHIVE
        if not archive.exists():
            print("Downloading the pinned official Windows compiler archive", flush=True)
            with urlopen(URL, timeout=90) as source, archive.open("wb") as output:
                while block := source.read(1024 * 1024):
                    output.write(block)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError("Compiler archive checksum mismatch; refusing execution")
        compiler = tools / "w64devkit/bin/g++.exe"
        if not compiler.exists():
            subprocess.run([str(archive), "-y", "-o" + str(tools)], check=True)
        if not compiler.exists():
            raise RuntimeError("Portable compiler extraction did not produce g++.exe")
        print(subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0])
    subprocess.run([sys.executable, "-m", "pip", "install", "--upgrade", "--target", str(tools / "python"),
                    "--report", str(tools / "python-install.json"), "Jinja2==3.1.6"], check=True)
    print("Local test tools ready. Run: python tests/run_native.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
