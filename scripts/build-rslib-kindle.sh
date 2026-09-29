#!/bin/sh
set -eu

ANKI_RELEASE=${ANKI_RELEASE:-26.08}
ANKI_COMMIT=${ANKI_COMMIT:-666c2c64d4a1772c03948f5b667438da63ddaa76}
ANKINK_ROOT=${ANKINK_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
ANKI_ROOT=${ANKI_ROOT:-"$ANKINK_ROOT/third_party/anki"}
RUST_TOOLCHAIN=${RUST_TOOLCHAIN:-1.92.0}
RUST_MANIFEST=${ANKINK_RUST_MANIFEST:-"$ANKINK_ROOT/backend/rust/Cargo.toml"}
KINDLE_BUILD_DIR=${KINDLE_BUILD_DIR:-"$ANKINK_ROOT/cmake-build-kindle-rslib"}
KINDLE_ABI=${KINDLE_ABI:-armel}
: "${KINDLE_SDK_ROOT:?Set KINDLE_SDK_ROOT to the AnkINK SDK directory}"
: "${PROTOC:?Set PROTOC to a protoc executable}"
. "$ANKINK_ROOT/scripts/kindle-abi.sh"
kindle_abi_configure "$KINDLE_ABI"
if [ -n "${RUST_TARGET:-}" ] && [ "$RUST_TARGET" != "$KINDLE_RUST_TARGET" ]; then
  echo "RUST_TARGET=$RUST_TARGET does not match KINDLE_ABI=$KINDLE_ABI" >&2
  exit 2
fi
RUST_TARGET=$KINDLE_RUST_TARGET

if [ ! -d "$ANKI_ROOT/.git" ]; then
  echo "Clone Anki $ANKI_RELEASE into $ANKI_ROOT first." >&2
  exit 1
fi
if [ "$(git -C "$ANKI_ROOT" rev-parse HEAD)" != "$ANKI_COMMIT" ]; then
  echo "third_party/anki is not pinned to $ANKI_RELEASE ($ANKI_COMMIT)." >&2
  exit 1
fi

if [ "${ANKINK_SKIP_RUSTUP:-0}" != 1 ]; then
  rustup toolchain install "$RUST_TOOLCHAIN" --profile minimal
  rustup target add "$RUST_TARGET" --toolchain "$RUST_TOOLCHAIN"
fi

RUST_TARGET_ENV=$(printf '%s' "$RUST_TARGET" | tr '[:lower:]-' '[:upper:]_')
RUST_TARGET_VAR=$(printf '%s' "$RUST_TARGET" | tr '-' '_')
export "CARGO_TARGET_${RUST_TARGET_ENV}_LINKER=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-gcc"
export "CC_${RUST_TARGET_VAR}=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-gcc"
export "CXX_${RUST_TARGET_VAR}=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-g++"
export "AR_${RUST_TARGET_VAR}=$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-ar"
export "CFLAGS_${RUST_TARGET_VAR}=$KINDLE_ARCH_FLAGS"
export "CXXFLAGS_${RUST_TARGET_VAR}=$KINDLE_ARCH_FLAGS"

cargo "+$RUST_TOOLCHAIN" build --release --target "$RUST_TARGET" \
  --manifest-path "$RUST_MANIFEST"

RSLIB="${CARGO_TARGET_DIR:-$(dirname "$RUST_MANIFEST")/target}/$RUST_TARGET/release/libankink_anki_backend.a"

# The Docker build directory lives in a reusable named volume, while /workspace
# may be populated by rsync with preserved (and therefore older) mtimes. Ninja
# cannot detect changed source content when a cached object happens to be newer
# than that source. Clean only the small CMake build when its input content has
# changed; Cargo and Docker caches remain intact.
CMAKE_INPUT_FINGERPRINT=$(
  {
    printf '%s\n' "$ANKINK_ROOT/CMakeLists.txt"
    find "$ANKINK_ROOT/cmake" "$ANKINK_ROOT/include" "$ANKINK_ROOT/src" \
      -type f -print
  } | LC_ALL=C sort | while IFS= read -r ANKINK_INPUT; do
    sha256sum "$ANKINK_INPUT"
  done | sha256sum | awk '{print $1}'
)
CMAKE_FINGERPRINT_FILE="$KINDLE_BUILD_DIR/.ankink-cmake-input-fingerprint"
CMAKE_INPUTS_CHANGED=1
if [ -f "$CMAKE_FINGERPRINT_FILE" ] &&
   [ "$(cat "$CMAKE_FINGERPRINT_FILE")" = "$CMAKE_INPUT_FINGERPRINT" ]; then
  CMAKE_INPUTS_CHANGED=0
fi

KINDLE_ABI="$KINDLE_ABI" cmake -S "$ANKINK_ROOT" -B "$KINDLE_BUILD_DIR" \
  -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_TOOLCHAIN_FILE="$ANKINK_ROOT/cmake/kindle-debian-toolchain.cmake" \
  -DANKINK_BUILD_APP=ON -DANKINK_BUILD_TESTS=OFF -DANKINK_USE_RSLIB=ON \
  -DANKINK_RSLIB_LIBRARY="$RSLIB"
if [ "$CMAKE_INPUTS_CHANGED" -eq 1 ]; then
  echo "AnkINK CMake inputs changed; invalidating cached C++ objects."
  KINDLE_ABI="$KINDLE_ABI" cmake --build "$KINDLE_BUILD_DIR" --target clean
fi
KINDLE_ABI="$KINDLE_ABI" cmake --build "$KINDLE_BUILD_DIR"
printf '%s\n' "$CMAKE_INPUT_FINGERPRINT" > "$CMAKE_FINGERPRINT_FILE"
