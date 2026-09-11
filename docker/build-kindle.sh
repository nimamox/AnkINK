#!/bin/sh
set -eu

WORKSPACE=/workspace
OUTPUT=/out
CACHE=/cache
TEMP_ROOT=/tmp/ankink-rust-build

test -f "$WORKSPACE/CMakeLists.txt"
test -d /opt/anki/.git

# Cargo path dependencies are intentionally relative. Mirror only the small
# Rust wrapper under /tmp and link its pinned Anki dependency from the image.
rm -rf "$TEMP_ROOT"
mkdir -p "$TEMP_ROOT/backend" "$TEMP_ROOT/third_party"
cp -a "$WORKSPACE/backend/rust" "$TEMP_ROOT/backend/rust"
ln -s /opt/anki "$TEMP_ROOT/third_party/anki"

export ANKINK_ROOT="$WORKSPACE"
export ANKI_ROOT=/opt/anki
export ANKINK_RUST_MANIFEST="$TEMP_ROOT/backend/rust/Cargo.toml"
export ANKINK_SKIP_RUSTUP=1
export CARGO_HOME="$CACHE/cargo-home"
export CARGO_TARGET_DIR="$CACHE/cargo-target"
export KINDLE_BUILD_DIR="$CACHE/cmake-armel"
export KINDLE_SDK_ROOT=/opt/kindle-sdk
export KINDLE_ABI=armel
export PROTOC=/usr/local/bin/protoc
export CROSS_COMPILE=/opt/kindle-sdk/bin/arm-linux-gnueabi-
mkdir -p "$CARGO_HOME" "$CARGO_TARGET_DIR"

"$WORKSPACE/scripts/build-rslib-kindle.sh"

STAGE="$CACHE/package-stage"
rm -rf "$STAGE" "$OUTPUT/ankink" "$OUTPUT/extensions" "$OUTPUT/documents"
"$WORKSPACE/scripts/package-kindle.sh" \
  "$KINDLE_SDK_ROOT/armel" "$KINDLE_BUILD_DIR" "$STAGE"

# The Oasis/PW2 kernel is Linux 3.0.35. Exercise the exact packaged loader and
# libraries under that reported kernel version so an incompatible glibc cannot
# reach dist/ unnoticed.
qemu-arm -r 3.0.35 "$STAGE/lib/ld-linux.so.3" \
  --library-path "$STAGE/lib" "$STAGE/bin/ankinkd" --help >/dev/null

# Catch a stale CMake object/package before a UI that calls this endpoint can
# be paired with a daemon that silently falls through to its generic 404.
if ! grep -a -F '/api/card-action' "$STAGE/bin/ankinkd" >/dev/null; then
  echo "Packaged ankinkd is stale: /api/card-action is missing." >&2
  exit 1
fi

mkdir -p "$OUTPUT/extensions" "$OUTPUT/documents"
mv "$STAGE" "$OUTPUT/ankink"
mv "$OUTPUT/ankink/kual-extension/AnkINK" "$OUTPUT/extensions/AnkINK"
rmdir "$OUTPUT/ankink/kual-extension"
mv "$OUTPUT/ankink/library-launcher/AnkINK.sh" "$OUTPUT/documents/AnkINK.sh"
rmdir "$OUTPUT/ankink/library-launcher"

sha256sum "$OUTPUT/ankink/bin/ankinkd" > "$OUTPUT/ankink/ankinkd.sha256"
echo "Built Kindle bundle: $OUTPUT/ankink"
echo "Built KUAL extension: $OUTPUT/extensions/AnkINK"
echo "Built Library launcher: $OUTPUT/documents/AnkINK.sh"
