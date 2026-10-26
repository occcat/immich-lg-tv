#!/usr/bin/env python3
"""Build a deterministic webOS IPK without requiring the LG IDE."""
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tarfile
import zlib

ROOT = Path(__file__).resolve().parent.parent
APP = "com.immichtv.viewer"
VERSION = "0.1.0"

def icon():
    width = height = 160
    colors = [(95, 152, 255), (127, 124, 244), (232, 114, 156), (240, 173, 95), (106, 199, 174)]
    # Match assets/icons/brand.svg. Supersample the launcher bitmap too: webOS
    # appinfo requires PNG, while the in-app source remains a vector asset.
    circles = [(32, 17, 15), (47, 28, 15), (41, 46, 15), (23, 46, 15), (17, 28, 15)]
    pixels = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            samples = []
            for sy in range(4):
                for sx in range(4):
                    px, py = (x + (sx+.5)/4 - 16)/2, (y + (sy+.5)/4 - 16)/2
                    c = (13, 20, 32)
                    for (cx, cy, radius), color in zip(circles, colors):
                        if (px-cx)**2 + (py-cy)**2 < radius**2: c = color
                    if (px-32)**2 + (py-32)**2 < 8**2: c = (13, 20, 32)
                    samples.append(c)
            row.extend(round(sum(c[i] for c in samples)/16) for i in range(3))
        pixels.append(row)
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(pixels))) + chunk(b"IEND", b"")

def archive(files):
    out = io.BytesIO()
    # Set gzip mtime too, so equal source bytes produce equal packages.
    import gzip
    with gzip.GzipFile(fileobj=out, mode="wb", mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as tar:
            for name, content, mode in sorted(files):
                item = tarfile.TarInfo(name); item.size = len(content); item.mode = mode; item.mtime = 0; item.uid = item.gid = 0
                tar.addfile(item, io.BytesIO(content))
    return out.getvalue()

def member(name, data):
    header = f"{name + '/':<16}{0:<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode("ascii")
    return header + data + (b"\n" if len(data) % 2 else b"")

def main():
    binary = Path(sys.argv[1])
    prefix = f"./usr/palm/applications/{APP}"
    appinfo = {"id": APP, "version": VERSION, "vendor": "Immich TV contributors", "type": "native", "main": "immich-tv", "title": "Immich TV", "icon": "icon.png", "largeIcon": "icon.png", "resolution": "1920x1080", "bgColor": "#0a0f18", "requiredACG": []}
    files = [(f"{prefix}/immich-tv", binary.read_bytes(), 0o755), (f"{prefix}/appinfo.json", json.dumps(appinfo).encode(), 0o644), (f"{prefix}/icon.png", icon(), 0o644)]
    for file in sorted((ROOT / "assets").rglob("*")):
        if file.is_file(): files.append((f"{prefix}/assets/{file.relative_to(ROOT / 'assets').as_posix()}", file.read_bytes(), 0o644))
    for name in ["LICENSE", "NOTICE.md"]:
        path = ROOT / name
        if path.exists(): files.append((f"{prefix}/{name}", path.read_bytes(), 0o644))
    control = f"Package: {APP}\nVersion: {VERSION}\nSection: misc\nPriority: optional\nArchitecture: arm\nMaintainer: Immich TV contributors\nDescription: Native read-only Immich photo viewer for LG webOS\nInstalled-Size: {sum(len(v) for _, v, _ in files)//1024}\n\n".encode()
    data = b"!<arch>\n" + member("debian-binary", b"2.0\n") + member("control.tar.gz", archive([("./control", control, 0o644)])) + member("data.tar.gz", archive(files))
    out = ROOT / "dist" / f"{APP}_{VERSION}_arm.ipk"; out.parent.mkdir(exist_ok=True); out.write_bytes(data)
    (out.parent / "SHA256SUMS").write_text(hashlib.sha256(data).hexdigest() + "  " + out.name + "\n")
    print(f"Built {out} ({len(data):,} bytes)")

if __name__ == "__main__": main()
