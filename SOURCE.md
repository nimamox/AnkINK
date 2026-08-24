# Corresponding source

Official AnkINK binaries are built from the tagged source at
<https://github.com/nimamox/AnkINK>. The repository includes AnkINK's Docker
build definition, Rust/C++ bridge, cross-build and packaging scripts, UI assets,
and all AnkINK-authored source needed to reproduce the Kindle distribution.

The build pins Anki 26.08 commit
`666c2c64d4a1772c03948f5b667438da63ddaa76`. Its corresponding source is at
<https://github.com/ankitects/anki/tree/666c2c64d4a1772c03948f5b667438da63ddaa76>.
The exact Rust resolution is recorded in `backend/rust/Cargo.lock`; crate
sources and their embedded notices are obtained from the registries and Git
revisions recorded by Cargo.

Each binary GitHub release must identify its exact AnkINK source tag or commit
and provide equivalent, no-charge access to that source and the pinned Anki and
Rust sources. The GNU runtime libraries are unmodified Debian builds; their
exact package versions and source locations are recorded in
`THIRD_PARTY_NOTICES.md`.

Anyone redistributing the binary is responsible for preserving the license and
notices and for satisfying the source-delivery requirements of AGPL-3.0-or-later
and the applicable third-party licenses.
