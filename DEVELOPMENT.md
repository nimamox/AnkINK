# AnkINK development

This document covers host development, architecture, cross-compilation, and
developer deployment. For features, normal installation, and user-facing
security information, see [README.md](README.md).

## Architecture

AnkINK is a C++17 application with an ES5 HTML/CSS user interface rendered by
Amazon Mesquite:

```text
Mesquite HTML/CSS/ES5 UI -> XMLHttpRequest -> ankinkd (C++17) -> Anki rslib (Rust)
```

Mesquite owns painting, e-ink updates, touch input, and system integration.
There is no bundled WPE WebKit, Mesa, Wayland, DRM, EGL, or FBInk rendering
path. The launcher marks the window Whisper-Touch capable through the Kindle
window-manager utility, allowing AwesomeWM to deliver Oasis Page Up/Page Down
events directly to WebKit. `ankinkd` does not monitor `/dev/input` or provide
an input-polling endpoint.

Card mathematics is rendered before it reaches Mesquite by `katex-rs` 0.2.4 in
the native Rust backend. Mesquite loads the matching KaTeX 0.16.25 CSS and WOFF
fonts, but not KaTeX JavaScript. Supported delimiters are `\\(...\\)`,
`\\[...\\]`, `$$...$$`, `[$]...[/$]`, `[$$]...[/$$]`, and
`[latex]...[/latex]`. A bounded native cache reuses repeated formulas, while
narrow viewport-lazy repairs handle known old-Mesquite layout defects.

Release builds use the pinned official Anki backend for scheduling, queues,
template rendering, card updates, review history, synchronization, and media.
The lightweight SQLite implementation selected by a default native CMake build
exists for unit testing; it is not the release review engine.

## Host simulator

The simulator runs the same UI and official Anki Rust backend used by the
Kindle release. Private state is kept under `.ankink-simulator/`, including the
collection, media, host key, and log. The directory and the downloaded Anki
source under `third_party/anki/` are ignored by Git.

On macOS, install the native prerequisites:

```sh
brew install rust protobuf
```

Configure and run the simulator:

```sh
cmake -S . -B cmake-build-simulator \
  -DANKINK_BUILD_SIMULATOR=ON \
  -DANKINK_BUILD_TESTS=OFF
cmake --build cmake-build-simulator --target ankink_simulator
```

The first build downloads the pinned Anki source and Rust crates. The target
starts `ankinkd`, opens `http://127.0.0.1:9257/simulator/`, and stops the daemon
when the target is stopped. Device profiles exercise viewport dimensions,
orientation, and simulated physical page buttons; they do not certify physical
device compatibility.

For CLion, create a local CMake profile using `cmake-build-simulator` and these
options:

```text
-DANKINK_BUILD_SIMULATOR=ON -DANKINK_BUILD_TESTS=OFF
```

Build and run the `ankink_simulator` target, and stop that target to stop the
local daemon.

## CMake configuration

The main options are:

- `ANKINK_BUILD_APP`: build `ankinkd` and its UI assets.
- `ANKINK_BUILD_TESTS`: build and register native and frontend contract tests.
- `ANKINK_USE_RSLIB`: use the official Anki Rust backend.
- `ANKINK_BUILD_SIMULATOR`: build the host simulator and enable rslib.
- `ANKINK_RSLIB_LIBRARY`: path to a prebuilt native Anki backend library when
  rslib is enabled outside the simulator workflow.

The Kindle build uses `cmake/kindle-debian-toolchain.cmake`. See
[docs/KINDLE_BUILD.md](docs/KINDLE_BUILD.md) for the lower-level manual
cross-build path.

## Official Anki backend

The backend is pinned to Anki 26.08 commit
`666c2c64d4a1772c03948f5b667438da63ddaa76` and Rust 1.92.0. A manual Linux
build can prepare it with:

```sh
mkdir -p third_party
git clone --branch 26.08 https://github.com/ankitects/anki.git third_party/anki
git -C third_party/anki submodule update --init ftl/core-repo ftl/qt-repo

export KINDLE_SDK_ROOT=/path/to/ankink-sdk
export PROTOC=/path/to/protoc
./scripts/build-rslib-kindle.sh
```

Ubuntu's `protobuf-compiler` package provides `protoc`. SQLite and Rustls are
linked into the release backend, so no target-side package installation is
needed.

## Docker cross-build internals

The supported release build is:

```sh
./build_on_docker.sh
```

It builds or reuses the `kindle-dev-builder:local` image and the
`ankink-kindle-build-cache` named volume. The image contains Debian ARMEL and
ARMHF cross-toolchains/sysroots, Rust 1.92 with both ARM targets, Protobuf 29.3,
pinned Anki source, and Rust crates.
The build container is short-lived; the source tree is mounted read-only and
only `dist/` receives packaged output.

A remote build uses:

```sh
./build_on_docker.sh user@host
```

The local machine requires `ssh` and `rsync`; the remote requires `rsync`,
Docker, and working Docker access. The script synchronizes to the reusable
`/tmp/kindle-build-$USER/AnkINK` directory, excluding Git data, credentials,
`dist/`, simulator state, and local build output. It invokes the same local
Docker path remotely and copies `dist/` back only after a successful build.
Docker image layers and the named build-cache volume remain outside `/tmp` and
are reused by later builds.

The package layout is:

```text
dist/
├── ankink/
│   ├── armel/{bin,lib}/
│   ├── armhf/{bin,lib}/
│   ├── share/ankink/
│   └── ankink.sh
├── extensions/AnkINK/
└── documents/AnkINK.sh
```

For a release archive, archive the **contents** of `dist/`:

```sh
tar -C dist -czf AnkINK-kindle.tar.gz ankink extensions documents
```

The default build creates both runtimes. `KINDLE_ABI=armel` or
`KINDLE_ABI=armhf` requests a single-ABI developer build. Native dependency,
CMake, package, and runtime-library outputs remain isolated by ABI; Cargo uses
one cache root with target-triple-specific subdirectories.

`scripts/validate-kindle-runtime.sh` checks ELF machine/float ABI, interpreter,
shared-library closure, and then runs the packaged loader plus `ankinkd --help`
under QEMU user mode. The launcher is independently exercised with an internal
test-only ABI override to verify its executable, loader, library path, preload,
and shared asset choices. These checks do not emulate or validate Kindle
firmware services. In particular, ARMHF Mesquite, appmgrd/appreg registration,
Whisper Touch, physical buttons, touch, e-ink, and suspend/resume remain pending
physical KindleHF testing.

## Kindle runtime and deployment

For an SSH-accessible development Kindle:

```sh
bash push_over_ssh.sh root@device_ip
```

The script prefers `rsync` and falls back to `scp`. It updates the app,
KUAL extension, and Library launcher, but does not restart AnkINK.

On launch, the script stops stale AnkINK UI/backend instances, starts `ankinkd`
on `127.0.0.1:9257`, copies the Mesquite assets to a content-versioned path
under `/var/local/mesquite`, updates `/var/local/appreg.db`, and asks
`com.lab126.appmgrd` to launch the app. The manifest suppresses Mesquite's
otherwise-empty navigation strip. The Close button also stops the backend.

The refresh action calls an existing FBInk binary, including MRInstaller's PW2
build when present, solely to request a full display refresh. FBInk is not used
to render the application. Orientation mode is restored across launches, and
the global orientation lock is released when AnkINK closes.

## Tests and debugging

A normal native validation cycle is:

```sh
node --check assets/app-settings.js
cmake --build cmake-build-debug -j4
ctest --test-dir cmake-build-debug --output-on-failure
```

The simulator is the fastest UI correctness check, but old Mesquite behavior,
e-ink refreshes, physical buttons, font metrics, and native math layout should
be verified on a real Kindle. Runtime logs are written to `ankinkd.log` in the
installed app directory. The backend status endpoint is available locally at
`http://127.0.0.1:9257/api/status` while AnkINK is running.

## Source and dependency compliance

The exact official source and redistribution obligations are documented in
[SOURCE.md](SOURCE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
[LICENSE](LICENSE). Keep those files with every distribution and identify the
exact AnkINK and pinned Anki source used for binary releases.
