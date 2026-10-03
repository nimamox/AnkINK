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
optimization-profile-specific cache roots with target-triple-specific subdirectories.

`scripts/validate-kindle-runtime.sh` checks ELF machine/float ABI, interpreter,
shared-library closure, and then runs the packaged loader plus `ankinkd --help`
under QEMU user mode. The launcher is independently exercised with an internal
test-only ABI override to verify its executable, loader, library path, preload,
and shared asset choices. These checks do not emulate or validate Kindle
firmware services. The ARMHF runtime, Mesquite launch, Whisper Touch
integration, physical buttons, touch, and e-ink behavior have been verified on
a Kindle Oasis 3, but other ARMHF models and firmware versions still require
physical-device testing.

## Kindle runtime and deployment

For an SSH-accessible development Kindle:

```sh
bash push_over_ssh.sh root@device_ip
```

The script uses `rsync` when available on both machines and falls back to
`scp` otherwise. It updates the app,
KUAL extension, and Library launcher, but does not restart AnkINK.

On launch, the script stops stale AnkINK UI/backend instances, starts `ankinkd`
on `127.0.0.1:9257`, copies the Mesquite assets to a content-versioned path
under `/var/local/mesquite`, updates `/var/local/appreg.db`, and asks
`com.lab126.appmgrd` to launch the app. The manifest suppresses Mesquite's
otherwise-empty navigation strip. The Close button also stops the backend.

The manual and automatic refresh actions use the stock Kindle refresh-only
`eips` operation; FBInk is not required. Orientation mode is restored across
launches, and the global orientation lock is released when AnkINK closes.

## Native Night Mode

The moon/sun button reads effective display state and sends explicit Day/Night
requests to `/api/night-mode`; it only changes its icon, not page/card colors or
image pixels. Native inversion affects the entire display, including the Kindle
status bar and images. KOA3 uses verified `epdcMode` Y8/Y8INV. KOA1's ineffective
property is rolled back and checked before using grayscale 1/2 framebuffer
ioctls. Read-only observation and permission to write are separate: PW12
`hwtcon_v2`/`FB_VISUAL_MONO10`, packed 8-bpp grayscale 1/2 is observable and
verifies native `epdcMode`, but never qualifies for direct framebuffer writes.
The fallback still requires `mxc_epdc_fb`, packed 8-bpp, static pseudocolor and
grayscale 1/2. Unknown drivers/formats fail safely. Recovery and restoration
also use independent effective-state observation for native sessions.
Each real transition gets one stock `eips -s w=<visible_xres>,h=<visible_yres> -f`
refresh using fresh geometry. No FBInk is required, and successful no-ops do not
refresh (a previously failed refresh can be retried). Stock `eips` capability
inspection is cached once per process and accepts the PW12 `eips_v2`
compatibility tool; geometry is still read fresh for each refresh.

Both apps share `/var/local/kindledev-night-mode.restore{,.lock}` for exclusive
ownership and durable crash recovery. Startup reads hardware rather than forcing
the saved `nightMode`. Clean exit/SIGINT/SIGTERM restores preexisting inversion
without overwriting external changes; the next app launch recovers an interrupted
session. Close one app before opening the other, since Home can leave a backend
running. `--recover-display` supports manual recovery with the matching loader.
Old `nightPageMode`/`nightCardMode` values are ignored and dropped on resave.
Host/simulator execution never opens /dev/fb0 or takes the global lock: Night Mode
is unavailable there and simulator Refresh is a safe no-op.

Validation (2026-10-03): Potion 7/7 and AnkINK 8/8 host tests passed, plus both
ARMEL/ARMHF build/loader/dependency/QEMU-user checks. Both apps passed KOA3 and
physical KOA1 automated transitions, no-ops, stock refresh, preexisting-state
restoration, shared locking, SIGKILL recovery and cross-app journal recovery.
The user confirmed Potion's KOA1 panel/status-bar inversion and return to Day
without stale regions. Emulator tests validate firmware controls, not physical
waveforms, ghosting or power; private test firmware extends timeouts/disables
suspend. Cleanup removes 661 net frontend lines across both apps, plus obsolete
mode fields, serialization, launcher opt-in logic, tests and two night-logo assets.

PW12 follow-up (2026-10-03): both actual Mesquite moon/sun buttons passed
Night/Day transitions on `epdcMode`, independently confirmed by `hwtcon_v2`
grayscale 2/1. Linux builds use `<linux/fb.h>` visual/type symbols; macOS tests
have guarded host-only definitions. Host suites (8/8 AnkINK, 7/7 Potion) and both
ABI package/loader/dependency/QEMU checks passed. Both apps also passed physical
KOA1 transitions, no-ops, restoration and shared/crash recovery, and KOA3 native
transitions, no-ops, stock refresh, preexisting-state and cross-app recovery.
An extended PW12 relaunch run encountered an AnkINK startup heap error
(`double free or corruption`); fresh-boot checks passed after restarting the
emulator. That interrupted stress run is not counted as a pass, and its startup
failure remains unresolved. PW12 shared ownership and cross-app SIGKILL recovery
also passed through the running APIs; rapid normal relaunches encountered an
`appreg.db` registration lock, so that launcher stress run remains incomplete.

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

## Kindle optimization validation (2026-10-01)

The universal Kindle build uses **Release with explicit `-O2 -DNDEBUG`** for
C/C++, checked CMake IPO for Release targets, and Rust `opt-level = 2`,
`lto = "thin"`, `codegen-units = 1`. GCC flags are
`-march=armv7-a -mtune=generic-armv7-a -mfpu=neon`, with `-mfloat-abi=softfp`
for ARMEL and `-mfloat-abi=hard` for ARMHF. There is still exactly one runtime
per ABI, with the existing loaders and launcher selection. No device-specific
`-mcpu`, runtime dispatch, or `-ffast-math` was added.
OpenSSL/curl now build with fixed `-O2` and the same generic ARM flags;
the dependency-input fingerprint rebuilds the Docker image for these changes.
CMake and Cargo caches additionally separate optimization levels, Rust NEON,
and IPO by profile. Explicit flags replace cached CMake optimization flags;
source-content fingerprints invalidate C++ objects even after rsync preserves
old mtimes. Cargo tracks target flags and release profile changes. Kindle
builds clear inherited global Rust flags so the selected target profile wins.

Five developer configurations were compared: the original MinSizeRel/GCC `-Os`
and Rust `"s"` baseline; O2/Rust2 with and without Rust NEON; and O3/Rust3 with
and without Rust NEON. IPO is enabled in all four performance candidates.
The default keeps **O2/Rust2 and no additional Rust target features**: O3 and
Rust NEON did not improve the tested workloads consistently enough to justify
choosing them for the broad device range. C/C++ NEON remains enabled.

Rust NEON was tested, rather than inferred from compiler flags. ARMHF uses
`-C target-feature=+neon`. On the pinned Rust 1.92 ARMEL target, `+neon` alone
crashes LLVM during the math static-library build ("Do not know how to soften
this operator's operand!"). The working experiment uses
`-C target-feature=-soft-float,+vfp3,+neon`; the target's soft calling convention
is retained. Rust's [pinned ABI feature checks](https://github.com/rust-lang/rust/blob/1.92.0/compiler/rustc_target/src/target_features.rs)
document this ARM softfp equivalent. These experimental features generate
compiler instability warnings, another reason to require measured benefit.
Disassembly of `katex::build_html::build_html` contains NEON `vld1.32` and
`vst1.32` instructions. `scripts/check-rust-float-abi.sh` checks ELF VFP argument
attributes and C-to-Rust/Rust-to-C f32/f64 calls under both Cortex-A8 and
Cortex-A9 QEMU CPUs for each ABI; the Docker build runs this automatically.

Measurements use three runs of each configuration, reporting the median of
per-run medians in milliseconds. Standalone benchmarks call production code
with generated, disposable fixtures, without credentials or remote HTTP.
All benchmark configurations use the same rebuilt O2 native dependency SDK;
the baseline uses frozen original Rust archives and original C++ flags.
Daemon size comparisons use the actual original and candidate distributions.
QEMU timing is diagnostic, with shared-host scheduling noise; it is not a
prediction of Kindle latency. Physical ARMEL measurements use an Oasis 1,
firmware 5.16.2.1.1, temporarily held at 996 MHz for fair comparison; its
`ondemand` governor is restored afterward. ARMHF performance was measured
under QEMU, not on physical ARMHF hardware. Backend HTML generation does not
measure Mesquite painting, network latency, or panel refresh.

The fixture generator `backend/rust/examples/benchmark_fixture.rs` uses pinned
Anki APIs to create 200 formatted/math notes across five decks. It refuses to
overwrite an existing path. The benchmark opens collections, lists decks and
review history, renders twelve distinct cards, then measures 40 card/answer/
undo cycles against a fresh copy. It never reads the personal collection.

Physical Oasis 1 (ARMEL):

| Workload (ms) | Baseline | O2/Rust2 | O2/Rust2 + Rust NEON | O3/Rust3 + Rust NEON | O3/Rust3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Collection open + decks | 49.230 | 48.160 | 47.633 | 47.727 | 47.613 |
| Deck tree | 2.029 | 1.955 | 1.944 | 1.824 | 1.797 |
| Review history | 9.552 | 8.354 | 8.423 | 8.154 | 8.223 |
| Cold card | 5.223 | 4.792 | 4.676 | 4.696 | 4.682 |
| Warm card | 1.930 | 1.743 | 1.732 | 1.756 | 1.681 |
| Answer Good | 8.845 | 8.733 | 8.843 | 8.906 | 8.728 |
| Undo | 8.979 | 8.957 | 8.836 | 8.877 | 8.890 |

ARMHF under Cortex-A8 QEMU (diagnostic):

| Workload (ms) | Baseline | O2/Rust2 | O2/Rust2 + Rust NEON | O3/Rust3 + Rust NEON | O3/Rust3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Collection open + decks | 23.043 | 21.056 | 21.111 | 21.001 | 21.091 |
| Deck tree | 0.766 | 0.706 | 0.774 | 0.690 | 0.670 |
| Review history | 4.556 | 3.522 | 3.860 | 3.728 | 3.548 |
| Cold card | 2.006 | 1.622 | 1.692 | 1.687 | 1.601 |
| Warm card | 0.617 | 0.498 | 0.536 | 0.499 | 0.490 |
| Answer Good | 0.620 | 0.593 | 0.603 | 0.574 | 0.572 |
| Undo | 0.538 | 0.523 | 0.531 | 0.505 | 0.506 |

Stripped packaged daemon sizes (bytes):

| Configuration | ARMEL | ARMHF |
| --- | ---: | ---: |
| Baseline | 19,302,088 | 18,056,900 |
| O2/Rust2 | 21,333,224 | 19,891,424 |
| O2/Rust2 + Rust NEON | 21,136,616 | 19,694,816 |
| O3/Rust3 + Rust NEON | 22,119,576 | 20,546,704 |
| O3/Rust3 | 22,381,720 | 20,743,312 |

Raw median/p95/sample-count records, including ARMEL QEMU, are in
[`tests/benchmark_results/2026-10-01.csv`](tests/benchmark_results/2026-10-01.csv).

Validation passed the existing three host CTest suites for this project in both
the original build and Release/O2 with IPO, JavaScript syntax checks, and all
ARMEL/ARMHF ELF, packaged-loader, dependency-closure, launcher-selection, and
QEMU `/api/status` checks for baseline and all candidates. The final default
universal package passes them again, including the new float ABI probes.
GCC reports vectorized blocks in production code (AnkINK HTTP handling and
Potion image-cache metadata). Math/card HTML from all 30 QEMU runs and 15
physical runs per project matches baseline after sorting unique inline-style
declarations and HTML attributes; text, values, and structure remain exact.
Randomized Rust map iteration makes byte hashes of HTML unsuitable for this
comparison. No installed application, account data, telemetry, or releases
were changed by these tests.

To reproduce candidate builds with the normal build entry point:

```sh
# Chosen default: O2 C/C++, Rust2, checked IPO, original Rust target features.
bash build_on_docker.sh user@host
# O3 comparison, without changing ABI or adding runtime variants:
KINDLE_CPP_OPT_LEVEL=3 KINDLE_RUST_OPT_LEVEL=3 bash build_on_docker.sh user@host
# Optional experimental Rust SIMD and GCC vector reports:
KINDLE_RUST_NEON=1 KINDLE_VECTOR_REPORT=ON bash build_on_docker.sh user@host
```

`KINDLE_IPO=OFF` allows a developer IPO comparison. The scripts accept only
optimization levels 2/3, NEON 0/1, and IPO/report ON/OFF. Each command packages
one chosen implementation per ABI, not all benchmark candidates.
Enable the optional CMake `ANKINK_BUILD_BENCHMARKS=ON` in a separate build
configured with the same toolchain, flags, IPO, and imported Rust archive; build
the `workload_benchmark` target. It is never copied into `dist/`. Run through
the matching packaged loader and library path, on hardware or under
`qemu-arm -r 3.0.35 -cpu cortex-a8`. Preserve each candidate's archive and
binary before changing profiles; use a fresh private fixture directory per
process. Keep timing runs serial and compare repeated results, not one noisy
sample. Avoid interpreting cache/fsync p95 spikes as compiler improvements.

Generate the fixture using the pinned Anki environment and native target:
`cargo +1.92.0 run --locked --release --manifest-path backend/rust/Cargo.toml
--example benchmark_fixture -- /tmp/benchmark.anki2`. Save the printed first
deck ID. Pass a **fresh writable copy** and that ID to `workload_benchmark`;
it performs actual reviews/undo on the copy. `--startup-only` isolates the
first card without the answer-write workload. The chosen O2 package grows
10.5% ARMEL / 10.2% ARMHF; O3 adds another 4.9% / 4.3% while the physical warm
card gain over O2 is only about 0.06 ms. O2 improves cold/warm card medians
8.3% / 9.7% versus baseline. Physical answer/undo medians are effectively
unchanged with long flash-write tails, so no answer-speed improvement is claimed.

First-card startup received an additional ten alternating baseline/O2 runs
without answer writes. Median first-card time was **48.94 → 51.86 ms**; the
observed ranges were 44.75–441.97 ms and 50.67–192.89 ms respectively. This is
a small startup cost, not a startup speedup, and is retained as a documented
tradeoff for faster repeated card/history workloads. Collection-open/deck
construction in those runs improved 49.96 → 47.40 ms. The original three-run
first-card medians were dominated by flash/scheduling outliers and are retained
in the raw CSV rather than presented as stable performance gains. Host
collection tests use the stub backend; the ARM workload tests exercise the
actual official Anki backend and scheduler.
