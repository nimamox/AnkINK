![AnkINK](logo/ankink_logo_orig_size.png)

# AnkINK

AnkINK is a C++17 Anki reader for jailbroken Kindle devices. Its UI uses the
Kindle's built-in Mesquite application runtime; a small C++ daemon provides the
real Anki collection over a loopback-only HTTP API:

```text
Mesquite HTML/CSS/ES5 UI -> XMLHttpRequest -> ankinkd (C++17) -> Anki rslib (Rust)
```

There is no bundled WPE WebKit, Mesa, Wayland, DRM, EGL, or FBInk display path.
Mesquite owns painting, e-ink updates, touch input, and system integration.
The Kindle launcher marks AnkINK's window Whisper-Touch capable through the
firmware's window-manager utility, so AwesomeWM delivers Oasis page buttons
directly to WebKit as Page Up/Page Down key events. `ankinkd` does not monitor
`/dev/input` or expose an input-polling endpoint.
The manifest suppresses Mesquite's otherwise-empty navigation strip; AnkINK
provides compact Refresh and Close controls in its own header while retaining
the Kindle status row.

Card mathematics is rendered by the native Rust `katex-rs` 0.2.4 library in
`ankinkd`, before the card reaches Mesquite. Mesquite loads only the matching
KaTeX 0.16.25 CSS and fonts; it neither loads KaTeX JavaScript nor parses TeX.
Supported delimiters are `\\(...\\)`, `\\[...\\]`, `$$...$$`, `[$]...[/$]`,
`[$$]...[/$$]`, and `[latex]...[/latex]`. No network connection or target-side
Rust installation is required. A bounded native cache reuses repeated formulas,
while small viewport-lazy DOM repairs retain compatibility with old Mesquite.

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
as AnkINK's rendering path. The rotation control switches between automatic
sensor rotation and locking the Kindle's current orientation; the preference
is restored on the next launch and the global lock is released when AnkINK
closes.

The AnkiWeb password is exchanged for a host key and then discarded. The host
key is stored at `/var/local/ankink/host-key` with mode 0600, outside the
USB-visible `/mnt/us` filesystem. Full upload is deliberately not exposed.

## Host development

The host simulator runs the same HTML/CSS/JavaScript UI and official Anki Rust
backend as the Kindle build. It stores its private, persistent collection,
media, host key, and log under `.ankink-simulator/`. That directory and the
downloaded pinned Anki source under `third_party/anki/` are gitignored. After
the first AnkiWeb login and download, stopping and starting the simulator does
not require another login.

On macOS, install the native build prerequisites once:

```sh
brew install rust protobuf
```

Configure and run from a terminal with:

```sh
cmake -S . -B cmake-build-simulator \
  -DANKINK_BUILD_SIMULATOR=ON \
  -DANKINK_BUILD_TESTS=OFF
cmake --build cmake-build-simulator --target ankink_simulator
```

The first build downloads the pinned Anki source and Rust crates. The target
starts `ankinkd`, opens `http://127.0.0.1:9257/simulator/` in the default
browser, and stops the daemon when the target is stopped. The wrapper defaults
to the Kindle Oasis 8th generation and can switch among the supported Kindle
profiles. Its Backward/Forward buttons and Page Up/Page Down or arrow keys call
a simulator-only front-end hook, without involving the daemon. The orientation
selector reflows the selected device between portrait and landscape and sends
the same `orientationchange` event used by Mesquite.

For CLion, create a CMake profile named `Host Simulator` using the local Apple
Clang toolchain and `cmake-build-simulator` build directory. Add these CMake
options:

```text
-DANKINK_BUILD_SIMULATOR=ON -DANKINK_BUILD_TESTS=OFF
```

Reload CMake, then build/run the `ankink_simulator` target. Stop that target in
CLion to stop the local daemon. Normal non-simulator host builds can still use
the lightweight SQLite fallback for unit tests.

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
Linux host or host-side ARM toolchain is required. To build with local Docker,
run:

```sh
./build_on_docker.sh
```

Docker can optionally run on a remote machine instead. The Mac needs `ssh` and
`rsync`; the remote machine needs `rsync`, Docker, and access to a working Docker
daemon. Pass its SSH target to the same script:

```sh
./build_on_docker.sh user@build-host
```

Remote mode synchronizes the working tree to the reusable
`/tmp/kindle-build-$USER/AnkINK` directory, excluding Git data, credentials,
`dist/`, simulator state, and local build artifacts. It then runs
`bash build_on_docker.sh` without arguments on that machine and copies the
completed remote `dist/` back only if the build succeeds. The remote directory,
fingerprinted Docker image layers, and named Docker cache volume remain
available for later builds; the Docker caches are not stored under `/tmp`.

The first build downloads the pinned Debian ARMEL cross-toolchain, Rust 1.92,
Protobuf 29.3, the pinned Anki 26.08 source, and Rust crates including
`katex-rs`. Later
runs reuse Docker layers and the `ankink-kindle-build-cache` Docker volume.
The container is short lived (`docker run --rm`); source is mounted read-only
and only `dist/` receives build artifacts.

The resulting USB layout is:

```text
dist/
├── ankink/                 # /mnt/us/ankink
├── extensions/
│   └── AnkINK/             # /mnt/us/extensions/AnkINK (KUAL)
└── documents/
    └── AnkINK.sh           # /mnt/us/documents/AnkINK.sh (Library)
```

For a user release, archive the **contents** of `dist/`, preserving those three
top-level directories. The user extracts it on a computer, copies `ankink`,
`extensions`, and `documents` to the top level of the mounted Kindle USB drive,
then safely ejects. Copying the enclosing `dist` directory is incorrect. The
Library entry requires PEKI, the same script-launcher support used by a
Library-installed `KUAL.sh`; KUAL remains an optional second launch route.

For example, after building:

```sh
tar -C dist -czf AnkINK-kindle.tar.gz ankink extensions documents
```

To install the result over SSH instead of USB:

```sh
./push_over_ssh.sh root@192.168.8.195
```

The deployment script prefers `rsync` and falls back to `scp`. It updates
AnkINK's app directory, KUAL extension, and Library launcher and does not
restart the application; relaunch AnkINK from the Library or KUAL afterwards.

Cross-build with `cmake/kindle-debian-toolchain.cmake`, then create the minimal
runtime bundle using `scripts/package-kindle.sh`. On first launch, sign into
AnkiWeb; AnkINK creates
its private collection under `/var/local/ankink` and downloads from AnkiWeb.

The single KUAL action stops any stale AnkINK UI/backend, starts a fresh
`ankinkd`, installs the Mesquite assets under a content-versioned path in
`/var/local/mesquite`, registers `org.ankink.app` in `/var/local/appreg.db`, and
asks `com.lab126.appmgrd` to launch it. The UI Close button stops the backend as
well as leaving the interface. The launcher does not stop `lab126_gui`.

## License and source

AnkINK is Copyright (C) 2026 AnkINK contributors and is free software licensed
under the [GNU Affero General Public License v3.0 or later](LICENSE). It
statically incorporates the pinned AGPL-3.0-or-later Anki backend, so every
binary release must identify and provide equivalent access to the exact AnkINK,
Anki, and Rust corresponding sources. See [SOURCE.md](SOURCE.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

AnkINK is an independent project and is not affiliated with or endorsed by
Ankitects. Anki and AnkiWeb are used only to identify compatibility; AnkINK
does not distribute Anki's official logo.
