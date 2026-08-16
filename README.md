# AnkINK

AnkINK is a C++17 Anki reader shell for jailbroken Kindle devices. Its display
stack is deliberately toolkit-free:

```text
HTML/CSS study UI → WPE WebKit → WPEBackend-fdo exportable buffer
                  → Y8 conversion + dirty rectangle → FBInk → Kindle EPDC
```

The initial target is the ARMv7 Kindle Oasis (8th generation). The repository
contains a real WPE/FBInk backend path, evdev touch input, an e-ink-oriented web
UI, read-only access to modern `collection.anki2` files, session review, an
ARMv7 CMake toolchain, and a relocatable bundle script.

## What works

- WPEBackend-fdo shared-memory and linear DMA-BUF frame import.
- Fast BGRA/RGBA to grayscale conversion for FBInk's Kindle Y8 path.
- One merged dirty rectangle per WebKit frame, partial updates, and periodic
  flashing maintenance refreshes to control ghosting.
- Kindle touchscreen discovery plus Oasis page-button dispatch using evdev.
- Deck listing and basic front/additional-field review from a modern Anki
  SQLite collection.
- A native JavaScript bridge with no HTTP server and a private `ankink://` asset
  scheme.

The current review engine is intentionally read-only: ratings retire cards for
the running session but do not update Anki scheduling fields, write `revlog`, or
sync to AnkiWeb. Card templates, cloze expansion, media lookup, scheduler parity,
and safe bidirectional sync are the next application layer. This distinction is
important—using a guessed scheduler would silently damage a real collection.

## Host verification

The renderer-independent core and collection reader can be built and tested on
macOS or Linux without WPE or FBInk:

```sh
cmake -S . -B build-host -DANKINK_BUILD_APP=OFF
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

## Native Linux build

Install a maintained WPE WebKit stable release (the code's minimum API is 2.38),
libwpe and WPEBackend-fdo 1.14 or newer, Wayland server headers, libdrm,
SQLite, and a full FBInk build with image support.
Then run:

```sh
cmake -S . -B build -DANKINK_BUILD_APP=ON
cmake --build build --parallel
sudo cmake --install build
```

The app requires a Linux framebuffer and will not open a desktop window. For
ARMv7 cross-compilation and Kindle deployment, see
[`docs/KINDLE_BUILD.md`](docs/KINDLE_BUILD.md).

## Collection

Place a recent uncompressed `collection.anki2` at
`/mnt/us/ankink/collection.anki2`, or pass another path with `--collection`.
AnkINK never modifies that file in this version.

## Security and licensing

The original `anki-cpp-example/config.json` contained live-looking plaintext
credentials. They have been replaced with placeholders and ignored by Git. If
those credentials were real, rotate the AnkiWeb password; removing a secret from
a working tree does not remove it from logs, backups, or repository history.

FBInk is GPLv3+. Distributing an AnkINK binary linked to FBInk requires a
GPL-compatible distribution plan and corresponding source obligations. WPE
WebKit and its dependency set have their own notices that must be shipped too.
