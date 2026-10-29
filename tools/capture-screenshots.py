#!/usr/bin/env python3
"""Capture the real SDL client against an isolated synthetic fixture service."""
from argparse import ArgumentParser
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

from mock_immich import FixtureServer, DEMO_KEY, MEDIA
from project import ROOT, NAME


def review_steps(path):
    steps = []
    def key(at, name): steps.append({"at": at, "key": name})
    def shot(at, name): steps.append({"at": at, "capture": str(path / f"{name}.png")})
    key(1800, "right"); shot(3000, "gallery")
    key(3200, "back"); key(3300, "down"); key(3400, "ok"); shot(4800, "albums")
    key(4900, "back"); key(5000, "down"); key(5100, "down"); key(5200, "ok")
    shot(6500, "videos"); key(6600, "back"); key(6700, "down"); key(6800, "ok")
    shot(7200, "settings"); key(7400, "ok"); shot(7800, "keyboard")
    return steps


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".local/screenshots/desktop")
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    binary = ROOT / "build" / f"{NAME}-qa"
    if not binary.exists(): raise SystemExit("Run make QA=1 host first.")
    if not (MEDIA / "photo-00.jpg").exists(): raise SystemExit("Run make fixtures first.")
    server = FixtureServer(("127.0.0.1", 0))
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix=f"{NAME}-screens-") as temp:
            params = {"server": f"http://127.0.0.1:{server.server_port}", "apiKey": DEMO_KEY,
                      "allowHttp": True, "qa": review_steps(output), "exitAfterMs": 8200}
            subprocess.run([str(binary), "--hidden", "--config", str(Path(temp) / "connection.json"),
                            json.dumps(params)], cwd=ROOT, env=dict(os.environ, SDL_VIDEODRIVER="dummy"),
                           check=True, timeout=30)
        states = {name: json.loads((output / f"{name}.png.json").read_text())
                  for name in ["gallery", "albums", "videos", "settings", "keyboard"]}
        assert states["gallery"]["count"] == 40 and states["gallery"]["columns"] == 5
        assert states["albums"]["count"] == 6 and states["albums"]["columns"] == 3
        assert states["videos"]["count"] == 3 and states["videos"]["columns"] == 3
        assert states["settings"]["settings"] and states["keyboard"]["keyboard"]
        assert not any(s["loading"] for s in states.values())
        assert all(r["method"] in ("GET", "HEAD") or
                   (r["method"] == "POST" and r["path"] == "/api/search/metadata")
                   for r in server.requests)
        (output / "provenance.json").write_text(json.dumps({"renderer": "native SDL desktop QA",
            "binary": binary.name, "fixture": True, "states": states}, indent=2) + "\n")
        print(f"Captured five native SDL screens: {output}")
    finally:
        server.shutdown(); server.server_close(); thread.join()


if __name__ == "__main__": main()
