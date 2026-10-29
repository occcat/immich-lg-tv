# Screenshots

The top-level PNGs in this directory were captured from the real SDL application on an
LG OLED48C4PCA with webOS platform 11.2.0, at 1920 × 1080. The displayed library
uses public test photography and synthetic records from the local fixture
service. No personal photo library or real account credential is shown.

| File | Screen |
| --- | --- |
| `gallery.png` | Five-column, three-row photo library. |
| `albums.png` | Three-column album covers. |
| `videos.png` | Native video library with duration badges. |
| `settings.png` | Connection form and slideshow settings. |
| `keyboard.png` | On-screen keyboard. |
| `empty.png` | Empty album. |

The layout was checked on the TV on 2026-10-26. To capture the current refactored
code on a desktop without a TV:

```sh
make fixtures
make QA=1 host
.venv/bin/python tools/capture-screenshots.py
```

The command starts an isolated fixture server, captures the actual SDL renderer,
checks navigation state, and writes PNGs plus `provenance.json` to
`.local/screenshots/desktop/`. The manual CI workflow performs the same capture
with deterministic offline fixtures and uploads it as an artifact.

## Current modular build

`desktop/` contains five fresh captures from the refactored client on
2026-10-29, rendered by the native desktop SDL QA binary at 1280 × 720.
`desktop/provenance.json` records the fixture and application states.
The television was unreachable for this capture, so these are explicitly
desktop captures rather than new TV captures.
