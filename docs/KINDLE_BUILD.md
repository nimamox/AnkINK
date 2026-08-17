# Building AnkINK for PW2-compatible ARMEL Kindles

The supported release build is Docker-based and works from macOS (including
Apple Silicon) and Linux. It cross-compiles an ARMEL binary for the Kindle;
the development machine's CPU architecture is not relevant.

## Build

Install Docker Desktop or Docker Engine and run from the repository root:

```sh
./build_on_docker.sh
```

The script builds/reuses `ankink-kindle-builder:local`, then runs a temporary
container with the repository mounted read-only and `dist/` mounted as output.
The Docker image contains the pinned Anki 26.08 source, Rust 1.92, Protobuf
29.3, and a Debian Bullseye ARMEL sysroot. Cargo and CMake intermediates persist
in the `ankink-kindle-build-cache` Docker volume.

The ready-to-install result is:

```text
dist/ankink/
dist/extensions/AnkINK/
```

`dist/ankink/ankinkd.sha256` contains the SHA-256 of the daemon.

## Install over SSH

```sh
./push_over_ssh.sh root@192.168.15.244
```

This copies `dist/ankink/` to `/mnt/us/ankink/` and the KUAL extension to
`/mnt/us/extensions/AnkINK/`. It does not delete unrelated Kindle files or
restart AnkINK; reopen it from KUAL after the transfer.

## Manual cross-build

`scripts/build-rslib-kindle.sh` remains usable with an externally supplied
`KINDLE_SDK_ROOT`, `PROTOC`, and pinned Anki checkout for advanced debugging.
The Docker path is preferred for normal release builds.
