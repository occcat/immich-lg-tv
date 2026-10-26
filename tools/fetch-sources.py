#!/usr/bin/env python3
"""Keep complete corresponding FFmpeg sources beside every distributed IPK."""
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parent.parent

def main():
    manifest = json.loads((ROOT / "third-party-sources.json").read_text())["ffmpeg"]
    output = ROOT / "dist"; output.mkdir(exist_ok=True)
    for key in ["archive", "build_script"]:
        item = manifest[key]; path = output / item["name"]
        if not path.exists():
            print(f"Fetching corresponding source: {path.name}", flush=True)
            with urllib.request.urlopen(item["url"], timeout=90) as response: data = response.read()
            if hashlib.sha256(data).hexdigest() != item["sha256"]: raise SystemExit("Source checksum mismatch")
            path.write_bytes(data)
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            raise SystemExit(f"Existing source checksum mismatch: {path}")
    print("Corresponding FFmpeg source and configuration verified.")

if __name__ == "__main__": main()
