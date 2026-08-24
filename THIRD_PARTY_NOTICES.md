# Third-party notices

AnkINK is Copyright (C) 2026 AnkINK contributors and is licensed as a whole
under GNU AGPL-3.0-or-later. The full project license is in `LICENSE`.

The following components are linked into or distributed with the Kindle
release. Their original licenses and notices continue to apply to them.

## Anki 26.08 Rust backend

- Upstream: <https://github.com/ankitects/anki>
- Pinned commit: `666c2c64d4a1772c03948f5b667438da63ddaa76`
- License: AGPL-3.0-or-later, with portions contributed under BSD-3-Clause
- Use: Anki `rslib` and `anki_proto` are statically linked into `ankinkd`

Copyright Ankitects Pty Ltd and Anki contributors. The upstream notice is
included as `THIRD_PARTY/Anki-LICENSE`. AnkINK is an independent combined work;
it is not affiliated with or endorsed by Ankitects. AnkINK does not distribute
Anki's official logo.

## Rust dependency closure

The pinned Rust dependency graph is recorded by `THIRD_PARTY/Cargo.lock`.
Anki's generated dependency authors/license inventory is included as
`THIRD_PARTY/Anki-cargo-licenses.json`. It contains AGPL-3.0-or-later and
compatible permissive or weak-copyleft components, including Apache-2.0, MIT,
BSD, ISC, MPL-2.0, Unicode-3.0, Zlib, and other explicitly listed alternatives.
Corresponding crate sources and their complete notices are part of the
Corresponding Source described in `SOURCE.md`.

The bundled SQLite implementation used through `rusqlite` is dedicated to the
public domain by its upstream authors: <https://www.sqlite.org/copyright.html>.

## KaTeX

- Upstream: <https://github.com/KaTeX/KaTeX>
- License: MIT, reproduced in `LICENSES/KaTeX-MIT.txt` and in the bundled
  `share/ankink/vendor/katex/LICENSE`
- Use: bundled JavaScript, CSS, and fonts for local mathematical rendering

Copyright (C) 2013-2020 Khan Academy and other contributors.

## GNU runtime libraries

The Kindle bundle includes unmodified runtime files from Debian cross packages:

- glibc 2.41 (`libc6-armel-cross` 2.41-11cross1): `ld-linux.so.3`, `libc.so.6`,
  and `libm.so.6`; LGPL-2.1-or-later. Source:
  <https://sources.debian.org/src/glibc/2.41-11/>
- GCC 14.2.0 (`libgcc-s1-armel-cross` and `libstdc++6-armel-cross`
  14.2.0-19cross1): `libgcc_s.so.1` and `libstdc++.so.6`; GPL-3.0-or-later
  with GCC Runtime Library Exception 3.1. Source:
  <https://sources.debian.org/src/gcc-14/>

The applicable texts are reproduced in `LICENSES/LGPL-2.1-or-later.txt`,
`LICENSES/GPL-3.0-or-later.txt`, and `LICENSES/GCC-exception-3.1.txt`.
Principal Apache-2.0 and MPL-2.0 texts used by the Rust dependency closure are
also reproduced in `LICENSES/`.

## Project artwork and trademarks

The original AnkINK names, logos, thumbnails, and other project artwork in
this repository are licensed under AGPL-3.0-or-later with the rest of AnkINK.
No trademark license is granted by the software license. Anki and AnkiWeb are
names of Ankitects products and services and are used only to identify
compatibility.
