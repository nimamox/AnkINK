# AnkINK

AnkINK is a C++17 Anki reader for jailbroken Kindle devices. Its UI uses the
Kindle's built-in Mesquite application runtime; a small C++ daemon provides the
real Anki collection over a loopback-only HTTP API:

```text
Mesquite HTML/CSS/ES5 UI -> XMLHttpRequest -> ankinkd (C++17) -> Anki rslib (Rust)
```

There is no bundled WPE WebKit, Mesa, Wayland, DRM, EGL, or FBInk display path.
Mesquite owns painting, e-ink updates, touch input, and system integration.
The manifest suppresses Mesquite's otherwise-empty navigation strip; AnkINK
provides compact Refresh and Close controls in its own header while retaining
the Kindle status row.

Card mathematics is rendered locally with the ES5-compatible KaTeX 0.13.24
distribution. Supported delimiters are `\\(...\\)`, `\\[...\\]`, `$$...$$`,
`[$]...[/$]`, `[$$]...[/$$]`, and `[latex]...[/latex]`. No network connection
is required for equations or fonts. KaTeX's MIT license is included beside the
vendored files under `assets/vendor/katex`.

Kindle release builds use the official Anki 26.08 Rust backend. Card queues,
template rendering, button intervals, FSRS/legacy scheduling, card updates and
revlog entries therefore come from Anki itself. AnkINK supports AnkiWeb login,
normal collection sync, and a guarded full download. It does not edit notes,
decks, templates, or deck options. Collection sync also downloads AnkiWeb
media into the private `/var/local/ankink/collection.media` directory.

The review toolbar shows Anki's live new/learning/review queue counts. Images
can be tapped to toggle a full-width view, and the header provides persistent
font-size and day/night controls. On an Oasis, Forward shows the answer and
then selects Good; Backward undoes the previous answer before reveal and
selects Again after reveal. Refresh requests a full flashing update through an
existing FBInk command (including MRInstaller's PW2 build) without using FBInk
as AnkINK's rendering path.

The AnkiWeb password is exchanged for a host key and then discarded. The host
key is stored at `/var/local/ankink/host-key` with mode 0600, outside the
USB-visible `/mnt/us` filesystem. Full upload is deliberately not exposed.

## Host development

```sh
cmake -S . -B build-host -DANKINK_BUILD_APP=ON -DANKINK_BUILD_TESTS=ON
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
./build-host/ankinkd --collection \
  "$HOME/Library/Application Support/Anki2/main_account/collection.anki2" \
  --assets ./assets
```

Open `http://127.0.0.1:8765/`. Host builds use the small SQLite fallback for UI
development and unit tests; scheduling and sync behavior must be tested with an
rslib build.

## Official Anki backend build

The backend is pinned to Anki 26.08 commit
`666c2c64d4a1772c03948f5b667438da63ddaa76` and Rust 1.92.0. On the Linux build
host:

```sh
mkdir -p third_party
git clone --branch 26.08 https://github.com/ankitects/anki.git third_party/anki
git -C third_party/anki submodule update --init ftl/core-repo ftl/qt-repo

export KINDLE_SDK_ROOT="$HOME/ankink-sdk"
export PROTOC=/path/to/protoc
./scripts/build-rslib-kindle.sh
```

Ubuntu's `protobuf-compiler` package provides `protoc`. No target-side package
installation is required; SQLite and Rustls are linked into `ankinkd`.

## Kindle deployment

### Docker cross-build (macOS or Linux)

Install and start Docker Desktop on macOS, or Docker Engine on Linux. No remote
Linux host or host-side ARM toolchain is required. Then run:

```sh
./build_on_docker.sh
```

The first build downloads the pinned Debian ARMEL cross-toolchain, Rust 1.92,
Protobuf 29.3, and the pinned Anki 26.08 source into the Docker image. Later
runs reuse Docker layers and the `ankink-kindle-build-cache` Docker volume.
The container is short lived (`docker run --rm`); source is mounted read-only
and only `dist/` receives build artifacts.

The resulting USB layout is:

```text
dist/
├── ankink/                 # copy to Kindle root as /mnt/us/ankink
└── extensions/
    └── AnkINK/             # copy to /mnt/us/extensions/AnkINK
```

To install the result over SSH instead of USB:

```sh
./push_over_ssh.sh root@192.168.15.244
```

The deployment script prefers `rsync` and falls back to `scp`. It only updates
AnkINK's two target directories and does not restart the application; relaunch
AnkINK from KUAL afterwards.

Cross-build with `cmake/kindle-debian-toolchain.cmake`, then create the minimal
runtime bundle using `scripts/package-kindle.sh`. Copy the resulting directory
to `/mnt/us/ankink` and copy `kual-extension/AnkINK` to
`/mnt/us/extensions/AnkINK`. On first launch, sign into AnkiWeb; AnkINK creates
its private collection under `/var/local/ankink` and downloads from AnkiWeb.

The KUAL action starts `ankinkd`, installs the Mesquite assets under
`/var/local/mesquite/ankink`, registers `org.ankink.app` in
`/var/local/appreg.db`, and asks `com.lab126.appmgrd` to launch it. It does not
stop `lab126_gui`.

AnkINK incorporates Anki's AGPL-3.0-or-later backend and is distributed under
compatible AGPL terms.
