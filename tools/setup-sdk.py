#!/usr/bin/env python3
"""Install the checksum-pinned webosbrew native toolchain used by PlxNative."""
import hashlib
from pathlib import Path
import platform
import subprocess
import tempfile
import urllib.request

VERSION = "webos-d7ed7ee.6"
ASSETS = {
    ("Darwin", "arm64"): ("darwin-arm64", "954333694ab1fe1f7737e81a0be1e3188cdfffc1a119c81c86f5fcebf1857c45"),
    ("Darwin", "x86_64"): ("darwin-x86_64", "75e7451ac39604872ae7f07894cbe8edb62b2091664a1781cb193e7ebe56aa75"),
    ("Linux", "aarch64"): ("linux-aarch64", "45a2d12ff557457d92cde4fddaa77a6f1090fca03adc43bb74397e5e0c379501"),
}

def main():
    target = Path.home() / "webos-ndk"
    sdk = target / "arm-webos-linux-gnueabi_sdk-buildroot"
    if (sdk / "bin/arm-webos-linux-gnueabi-gcc").exists():
        print(f"NDK already available: {sdk}"); return
    selected = ASSETS.get((platform.system(), platform.machine()))
    if not selected: raise SystemExit("No pinned NDK is available for this host; use macOS or Linux ARM64.")
    suffix, expected = selected
    name = f"arm-webos-linux-gnueabi_sdk-buildroot_{suffix}.tar.bz2"
    url = f"https://github.com/webosbrew/native-toolchain/releases/download/{VERSION}/{name}"
    target.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="immich-tv-ndk-") as folder:
        archive = Path(folder) / name
        print(f"Downloading {VERSION} ({suffix})…", flush=True)
        with urllib.request.urlopen(url, timeout=120) as response, archive.open("wb") as file:
            while chunk := response.read(1024*1024): file.write(chunk)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise SystemExit("NDK checksum mismatch; not extracting.")
        subprocess.run(["tar", "xjf", str(archive), "-C", str(target)], check=True)
        subprocess.run(["./relocate-sdk.sh"], cwd=sdk, check=True)
    print(f"NDK ready: {sdk}")

if __name__ == "__main__": main()
