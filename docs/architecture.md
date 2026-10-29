# Architecture

The app uses a 1920 × 1080 SDL canvas. The main thread owns SDL textures,
windowing, UI state transitions, and LG playback calls. Worker threads perform
HTTP/image work and container demuxing; results return through bounded queues.

## Application and interface

| Module | Responsibility |
| --- | --- |
| `src/main.c` | Process lifecycle, launch arguments, desktop/webOS events, and QA boundary. |
| `src/app.h` | Shared application state and application-layer entry points. |
| `src/app/catalog.c` | Catalog loading, worker results, generation checks, and API paging. |
| `src/app/viewer.c` | Opening/closing media, return position, slideshow timing, and playback state. |
| `src/app/settings.c` | Connection edits, keyboard text, persistence actions, and reconnecting. |
| `src/ui/layout.c` | Shared grid, sidebar, form geometry, and hit-test primitives. |
| `src/ui/theme.c` | Colors, font sizes, and navigation labels. |
| `src/ui/text.c` | Cached glyph rendering, visible-ink alignment, and UTF-8 label elision. |
| `src/ui/images.c` | Bounded image cache and asynchronous image requests. |
| `src/ui/draw.c` | Drawing primitives, icons, and image placement. |
| `src/ui/chrome.c` | Sidebar, headings, date/time, and remote-key hints. |
| `src/ui/gallery.c` | Photo/album/video cards, loading, errors, and empty states. |
| `src/ui/viewer.c` | Full-screen photo/video overlay and information panel. |
| `src/ui/settings.c` | Connection form and slideshow settings. |
| `src/ui/keyboard.c` | On-screen keyboard rendering and key labels. |
| `src/ui/remote.c` / `pointer.c` | Directional input and pointer dispatch using shared geometry. |
| `src/ui/lifecycle.c` | SDL, fonts, renderer setup, and cleanup. |
| `src/ui/screen.c` / `capture.c` | Screen composition and native screen capture. |
| `src/ui-paint.c` / `vector.c` | Antialiased rounded masks and NanoSVG icon rasterization. |

The internal UI header exposes shared rendering operations without compiling
implementation files into each other. The public `ui.h` contains lifecycle,
rendering, and input APIs. Application transitions stay in `src/app/`.

## Data and media

| Module | Responsibility |
| --- | --- |
| `src/common.c` | Bounded string copies and JSON field helpers. |
| `src/read-policy.c` | Server normalization, identifier validation, and request allowlist. |
| `src/config.c` | App-owned connection files and atomic persistence. |
| `src/catalog.c` | Asset/album parsing and Immich duration compatibility. |
| `src/navigation.c` | Grid movement and centered media geometry. |
| `src/api.c` | Dynamic libcurl binding, authenticated requests, catalog queries, and media ranges. |
| `src/worker.c` | Background catalog/image work and cancellation generations. |
| `src/demux.c` | FFmpeg container parsing, bounded packets, bitstream conversion, and seeking. |
| `src/media-webos.c` | LG hardware playback, callbacks, state, and native window placement. |
| `src/media.c` / `proxy.c` | Desktop ffplay preview over a temporary loopback-only media proxy. |

The request allowlist permits exact read endpoints and `POST /search/metadata`
for queries. Credentials use headers, redirects are not followed, and HTTPS
certificate checks stay enabled. The app does not write content to Immich.

The image cache holds 36 items and the text cache holds 256 entries. Demuxing
uses approximately 1 MiB range chunks and at most 128 packets / 16 MiB in the
packet queue. No per-frame HTTP, SVG parsing, or font rasterization is required
for already cached items.
