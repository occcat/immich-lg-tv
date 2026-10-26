# Third-party components

Immich TV is an independent, unofficial, read-only viewer. It is not affiliated
with Immich, FUTO, LG Electronics, Plex, or the PlxNative authors.

The application source is GPL-3.0-or-later. See LICENSE.

- `vendor/starfish.c` and `vendor/starfish.h` are copied from
  [GLinnik21/plx-native](https://github.com/GLinnik21/plx-native), commit
  `6ae7724bd3bb1541090b1799377bc3078a0fb528`, by Gleb Linnik,
  GPL-3.0-or-later. They provide the native media ABI and callback lifecycle
  adapter. Immich TV adds a declaration for the existing EOS verb and an unload
  acknowledgement accessor; other adapter behavior is retained. LG firmware
  binaries are not redistributed.
- `vendor/cJSON.c` and `vendor/cJSON.h`: cJSON 1.7.19, Dave Gamble and contributors,
  MIT. See `vendor/cJSON.LICENSE` (also bundled in the application assets).
- `assets/Interface.ttf`: the Noto Sans CJK font distributed by PlxNative;
  SIL Open Font License 1.1, copyright holders and full terms in `assets/OFL.txt`.
- `assets/cacert.pem`: Mozilla CA certificate bundle distributed by curl;
  certificate trust store, Mozilla Public License 2.0. See the source comments
  in the file and https://curl.se/docs/caextract.html.
- `assets/lib/libav{format,codec,util}-plx.so.*`: the unmodified LGPL FFmpeg
  9.0.2 shared libraries from PlxNative v0.9.0, released by GitHub Actions.
  The package SHA-256 was checked against that release before extraction.
  They are LGPL-2.1-or-later, dynamically linked and replaceable by the user.
  Complete pristine corresponding source (`dist/ffmpeg-9.0.2.tar.xz`) and the
  upstream build configuration (`dist/build-ffmpeg.sh`) accompany the IPK.
  `vendor/ffmpeg/` contains the matching API headers; `avconfig.h` declares the
  little-endian target facts. See `assets/FFmpeg.LGPL-2.1.txt`.
- SDL2, SDL2_image, SDL2_ttf, libcurl and LG media libraries are loaded from
  the host/television. Their binaries are not distributed in the IPK.

The original UI, icon, API client, worker, mock server and tests in this project
were implemented for Immich TV. PlxNative's name, logo, splash screen, and
trademark artwork are not reused. Demo photographs are downloaded separately
from Lorem Picsum/Unsplash; their source records are in
`fixtures/media/credits.json`. They are not part of the IPK.

API contracts were consulted from the official Immich OpenAPI at tag v3.3.5:
https://github.com/immich-app/immich/blob/v3.3.5/open-api/immich-openapi-specs.json
