#!/usr/bin/env python3
"""Package the X3/X4 application binary for SD/OTA updates (not a flash dump)."""

import configparser
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    config = configparser.ConfigParser()
    config.read(ROOT / "platformio.ini")
    version = config["crosspoint"]["version"]
    if not re.fullmatch(r"\d+\.\d+\.\d+-lxol\.\d+", version):
        raise SystemExit("Expected an A.B.C-lxol.N personal version")
    source = ROOT / ".pio/build/gh_release/firmware.bin"
    if not 0 < source.stat().st_size <= 0x640000:
        raise SystemExit("Firmware does not fit the X3 OTA slot")
    output = ROOT / "build/personal-release"
    output.mkdir(parents=True, exist_ok=True)
    name = f"crosspoint-{version}-x3-x4.bin"
    shutil.copyfile(source, output / name)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    (output / "SHA256SUMS").write_text(f"{digest}  {name}\n")

    def git(*args):
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()

    manifest = {
        "version": version,
        "source_commit": git("rev-parse", "HEAD"),
        "source_dirty": bool(git("status", "--porcelain", "--untracked-files=normal")),
        "sdk_commit": git("-C", "freeink-sdk", "rev-parse", "HEAD"),
        "base": "1.6.5",
        "file": name,
        "sha256": digest,
        "bytes": source.stat().st_size,
        "format": "ESP32-C3 application image; SD/OTA update, not a whole-flash image",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(output / name)
    print(f"{source.stat().st_size} bytes; SHA256 {digest}")


if __name__ == "__main__":
    main()
