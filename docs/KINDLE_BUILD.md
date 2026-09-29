# Building the universal AnkINK Kindle package

The supported release build is Docker-based and works from macOS (including
Apple Silicon) and Linux. It cross-compiles separate ARMEL and ARMHF runtimes;
the development machine's CPU architecture is not relevant.

## Build

Install Docker Desktop or Docker Engine and run from the repository root:

```sh
./build_on_docker.sh
```

The script builds/reuses `kindle-dev-builder:local`, then runs a temporary
container with the repository mounted read-only and `dist/` mounted as output.
The Docker image contains the pinned Anki 26.08 source, Rust 1.92, Protobuf
29.3, `katex-rs` 0.2.4, and Debian Trixie ARMEL and ARMHF sysroots. The same Rust
static library that supplies Anki's backend also renders card TeX to HTML, so
the package needs no Rust runtime or KaTeX JavaScript on the Kindle. Trixie's
glibc and required runtime closure are bundled separately for each ABI. Cargo
and per-ABI CMake intermediates persist in the
`ankink-kindle-build-cache` Docker volume.

The ready-to-install result is:

```text
dist/ankink/
dist/extensions/AnkINK/
dist/documents/AnkINK.sh
```

The native layout is:

```text
dist/ankink/
├── armel/bin/ankinkd
├── armel/lib/...
├── armhf/bin/ankinkd
├── armhf/lib/...
├── share/ankink/...       # shared UI/assets
├── etc/...                # shared CA bundle
└── ankink.sh              # automatic ABI selection
```

`ankinkd-armel.sha256` and `ankinkd-armhf.sha256` contain the daemon hashes.
For a single-ABI debugging build, set `KINDLE_ABI=armel` or
`KINDLE_ABI=armhf`; the default is the universal build.
The release helper packages this result as
`AnkINK-<version>-kindle-universal.tar.gz` and the equivalent `.zip` archive.

The build validates both ELF architectures, dynamic interpreters, ARM float
ABI attributes, and packaged shared-library closure. It also runs each bundled
loader and daemon `--help` path under QEMU user mode. This is a CPU/userspace
smoke test only: it is not a newer Kindle firmware emulator and cannot validate
Mesquite, `appmgrd`, `appreg.db`, e-ink, touch, page buttons, or suspend/resume.
ARMHF support has not yet been tested on physical KindleHF hardware.

## Install over SSH

```sh
./push_over_ssh.sh root@192.168.15.244
```

This copies `dist/ankink/` to `/mnt/us/ankink/`, the KUAL extension to
`/mnt/us/extensions/AnkINK/`, and the direct Library launcher to
`/mnt/us/documents/AnkINK.sh`. It does not delete unrelated Kindle files or
restart AnkINK; reopen it from the Library or KUAL after the transfer.

For a USB-installed release archive, preserve the contents of `dist/` at the
archive root. Users extract it on their computer and copy the `ankink`,
`extensions`, and `documents` directories to the top level of the mounted
Kindle drive. They do not need SSH.

## Manual cross-build

`scripts/build-rslib-kindle.sh` remains usable with an externally supplied
`KINDLE_SDK_ROOT`, `KINDLE_ABI`, `PROTOC`, and pinned Anki checkout for advanced
debugging. The Docker path is preferred for normal release builds.
