# License scope

## Original project code: MIT

The original Immich LG TV application code, UI, artwork, tooling, and tests by
occcat are available under the [MIT License](../LICENSE), except for the
third-party components and references listed in [NOTICE.md](../NOTICE.md).
The MIT copyright and permission notice must accompany copies of those parts.

This change does not remove licenses from third-party material or rewrite the
license history of earlier versions.

## Current webOS executable: GPL-3.0-or-later

`vendor/starfish.c` and `vendor/starfish.h` originate from PlxNative and are
GPL-3.0-or-later. The native webOS executable compiles and links that adapter
into the application. Its distribution remains subject to GPL-3.0-or-later,
including the corresponding-source requirements. MIT-licensed original modules
can be included in this GPL-compatible combination without relicensing the
original modules themselves.

The current IPK is therefore **not an MIT-only application distribution**.
It includes the root MIT text, the retained GPL text in
`assets/PlxNative.GPL-3.0.txt`, and the third-party notices.

Making the webOS application MIT licensed as a whole requires replacing the
GPL adapter with an independently authored, permissively licensed implementation
or obtaining a suitable license from its copyright holder. That also requires
revalidating native playback and callback lifetime behavior on a television.

The FSF explains the distinction between compatible components and the combined
program in its [GPL compatibility FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#WhatDoesCompatMean).
The standard MIT terms are published by the
[Open Source Initiative](https://opensource.org/license/mit).

## Other components

FFmpeg retains LGPL-2.1-or-later; its replaceable shared libraries and complete
corresponding sources must continue to travel together. NanoSVG, cJSON, the
font, the certificate bundle, and the OpenCode reference retain their respective
zlib, MIT, OFL, MPL, and MIT terms. The license change does not replace these
component-specific notices.
