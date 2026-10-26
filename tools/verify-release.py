#!/usr/bin/env python3
"""Check the release IPK, including the absence of development entry points."""
import hashlib
import io
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parent.parent

def ar_members(data):
    if data[:8] != b"!<arch>\n": raise ValueError("Invalid ar archive")
    offset = 8
    while offset < len(data):
        header = data[offset:offset+60]
        if len(header) != 60 or header[-2:] != b"`\n": raise ValueError("Invalid ar header")
        length = int(header[48:58]); name = header[:16].decode().strip().rstrip("/")
        offset += 60; yield name, data[offset:offset+length]; offset += length + length % 2
    if offset != len(data): raise ValueError("Truncated ar archive")

def main():
    path = ROOT / "dist/com.immichtv.viewer_0.1.0_arm.ipk"
    data = path.read_bytes(); members = dict(ar_members(data))
    assert members["debian-binary"] == b"2.0\n"
    prefix = "./usr/palm/applications/com.immichtv.viewer/"
    with tarfile.open(fileobj=io.BytesIO(members["data.tar.gz"]), mode="r:gz") as tar:
        names = {m.name for m in tar.getmembers()}
        info = json.load(tar.extractfile(prefix + "appinfo.json"))
        binary = tar.extractfile(prefix + "immich-tv").read()
        assert info["id"] == "com.immichtv.viewer" and info["version"] == "0.1.0" and info["type"] == "native"
        assert binary[:4] == b"\x7fELF" and int.from_bytes(binary[18:20], "little") == 40
        assert not any(marker in binary for marker in [b"QA frame captured", b"capturePath", b"exitAfterMs", b"immich-tv-demo"])
        assert all(name.startswith(prefix) and ".." not in name for name in names)
        assert sum("/assets/lib/" in name for name in names) == 3
        assert prefix + "assets/FFmpeg.LGPL-2.1.txt" in names
    checksum = hashlib.sha256(data).hexdigest()
    assert (ROOT / "dist/SHA256SUMS").read_text().split()[0] == checksum
    print(json.dumps({"package": path.name, "sha256": checksum, "bytes": len(data), "native_arm": True, "qa_entry_points": False, "bundled_ffmpeg_libraries": 3}, indent=2))

if __name__ == "__main__": main()
