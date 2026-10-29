#!/usr/bin/env python3
"""Launch only the synthetic fixture account on an already paired television."""
from argparse import ArgumentParser
from pathlib import Path
import json
import os
import shutil
import socket
import subprocess

from project import ROOT, APP, PACKAGE

def cli(name):
    executable = shutil.which(name)
    if executable: return executable
    override = os.environ.get("WEBOS_CLI_HOME")
    if override:
        candidate = Path(override) / "bin" / (name + ".js")
        if candidate.exists(): return str(candidate)
    fnm = Path.home() / "Library/Application Support/fnm/node-versions"
    candidates = list(fnm.glob(f"*/installation/bin/{name}")) if fnm.exists() else []
    if candidates: return str(max(candidates, key=lambda p: p.stat().st_mtime))
    raise SystemExit("Install @webos-tools/cli first.")

def main():
    parser = ArgumentParser(); parser.add_argument("--device", default="lgtv"); parser.add_argument("--server"); parser.add_argument("--install", action="store_true"); parser.add_argument("--smoke", action="store_true")
    args = parser.parse_args()
    server = args.server
    if not server:
        devices = json.loads((Path.home() / ".webos/tv/novacom-devices.json").read_text())
        device = next(d for d in devices if d.get("name") == args.device and d.get("profile") == "tv")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect((device["host"], 9922)); address = sock.getsockname()[0]
        server = f"http://{address}:22830"
    if args.install:
        subprocess.run([cli("ares-install"), "-d", args.device, str(ROOT / "dist" / PACKAGE)], check=True)
    subprocess.run([cli("ares-launch"), "-c", APP, "-d", args.device], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    params = {"server": server, "apiKey": "immich-tv-demo", "allowHttp": True, "remember": True}
    if args.smoke:
        # Requires `make QA=1 ipk`. The public fixture key is never a real account credential.
        steps = []
        def key(at, name): steps.append({"at": at, "key": name})
        def shot(at, name): steps.append({"at": at, "capture": f"/media/developer/temp/immich-tv-{name}.png"})
        key(3500,"right"); key(4000,"left")
        for _ in range(3): key(4200,"down")
        key(4500,"ok"); key(7000,"right"); key(7500,"ok"); key(8000,"ok")
        key(11000,"ok"); shot(12000,"pause"); key(12500,"ok"); key(13500,"left")
        key(16000,"back"); key(17000,"ok"); shot(22000,"video-repeat")
        key(24000,"back"); key(25000,"back"); key(26000,"left"); key(26500,"left")
        key(27000,"up"); key(27000,"up"); key(27500,"ok"); shot(31000,"albums")
        key(31500,"ok"); shot(35000,"album"); key(35500,"back"); key(39000,"back")
        key(39500,"up"); key(40000,"ok"); key(43000,"nextpage"); shot(46000,"page2")
        key(46500,"prevpage"); shot(50000,"gallery")
        params["qa"] = steps
    subprocess.run([cli("ares-launch"), "-d", args.device, APP, "-p", json.dumps(params)], check=True)
    print("Synthetic fixture account launched. Keep the local fixture server running.")

if __name__ == "__main__": main()
