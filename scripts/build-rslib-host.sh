#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 SOURCE_ROOT CARGO_TARGET_DIR" >&2
  exit 2
fi

ROOT=$1
TARGET=$2
ANKI_DIR="$ROOT/third_party/anki"
ANKI_COMMIT=666c2c64d4a1772c03948f5b667438da63ddaa76

command -v cargo >/dev/null || {
  echo "Rust is required. On macOS install it with: brew install rust" >&2
  exit 127
}
command -v protoc >/dev/null || {
  echo "Protobuf is required. Install it with: brew install protobuf" >&2
  exit 127
}

if [[ ! -d "$ANKI_DIR/.git" ]]; then
  mkdir -p "$ROOT/third_party"
  git clone https://github.com/ankitects/anki.git "$ANKI_DIR"
fi
if ! git -C "$ANKI_DIR" cat-file -e "${ANKI_COMMIT}^{commit}" 2>/dev/null; then
  git -C "$ANKI_DIR" fetch --quiet origin "$ANKI_COMMIT"
fi
git -C "$ANKI_DIR" checkout --quiet --detach "$ANKI_COMMIT"
git -C "$ANKI_DIR" submodule update --init ftl/core-repo ftl/qt-repo

export CARGO_TARGET_DIR="$TARGET"
export PROTOC="$(command -v protoc)"
cargo build --manifest-path "$ROOT/backend/rust/Cargo.toml" --release
