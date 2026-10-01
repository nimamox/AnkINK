#!/bin/sh
set -eu

WORKSPACE=/workspace
OUTPUT=/out
CACHE=/cache
TEMP_ROOT=/tmp/ankink-rust-build
REQUESTED_ABI=${KINDLE_ABI:-universal}

test -f "$WORKSPACE/CMakeLists.txt"
test -d /opt/anki/.git
. "$WORKSPACE/scripts/kindle-abi.sh"
. "$WORKSPACE/scripts/kindle-optimization.sh"
kindle_optimization_configure

case "$REQUESTED_ABI" in
  universal|all) BUILD_ABIS="armel armhf" ;;
  armel|armhf) BUILD_ABIS=$REQUESTED_ABI ;;
  *) echo "KINDLE_ABI must be universal, armel, or armhf" >&2; exit 2 ;;
esac

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
export CARGO_TARGET_DIR="$CACHE/cargo-target-$KINDLE_OPT_PROFILE"
export KINDLE_SDK_ROOT=/opt/kindle-sdk
export PROTOC=/usr/local/bin/protoc
mkdir -p "$CARGO_HOME" "$CARGO_TARGET_DIR"

for BUILD_ABI in $BUILD_ABIS; do
  kindle_abi_configure "$BUILD_ABI"
  kindle_rust_flags_configure
  sh "$WORKSPACE/scripts/check-rust-float-abi.sh"
  export KINDLE_BUILD_DIR="$CACHE/cmake-$KINDLE_ABI-$KINDLE_OPT_PROFILE"
  export CROSS_COMPILE="$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-"
  export RUST_TARGET="$KINDLE_RUST_TARGET"

  echo "Building AnkINK runtime for $KINDLE_ABI"
  "$WORKSPACE/scripts/build-rslib-kindle.sh"

  ABI_STAGE="$CACHE/package-stage-$KINDLE_ABI"
  rm -rf "$ABI_STAGE"
  "$WORKSPACE/scripts/package-kindle.sh" "$KINDLE_ABI" \
    "$KINDLE_SDK_ROOT/$KINDLE_ABI" "$KINDLE_BUILD_DIR" "$ABI_STAGE"
  "$WORKSPACE/scripts/validate-kindle-runtime.sh" "$KINDLE_ABI" "$ABI_STAGE"
done

FIRST_ABI=${BUILD_ABIS%% *}
COMMON_STAGE="$CACHE/package-stage-$FIRST_ABI"
PACKAGE_STAGE="$CACHE/universal-stage"
DIST_STAGE="$CACHE/dist-stage"
rm -rf "$PACKAGE_STAGE" "$DIST_STAGE"
mkdir -p "$PACKAGE_STAGE" "$DIST_STAGE/extensions" "$DIST_STAGE/documents"

# Architecture-independent content is copied once. Native executables,
# loaders, libraries, and the Mesquite preload remain isolated by ABI.
for COMMON_PATH in ankink.sh share etc README.txt LICENSE LICENSES SOURCE.md \
  THIRD_PARTY THIRD_PARTY_NOTICES.md kual-extension library-launcher; do
  cp -a "$COMMON_STAGE/$COMMON_PATH" "$PACKAGE_STAGE/$COMMON_PATH"
done
for BUILD_ABI in $BUILD_ABIS; do
  mkdir -p "$PACKAGE_STAGE/$BUILD_ABI"
  cp -a "$CACHE/package-stage-$BUILD_ABI/bin" \
    "$CACHE/package-stage-$BUILD_ABI/lib" "$PACKAGE_STAGE/$BUILD_ABI/"
  test ! -e "$PACKAGE_STAGE/$BUILD_ABI/share"
  (
    cd "$PACKAGE_STAGE"
    sha256sum "$BUILD_ABI/bin/ankinkd" > "ankinkd-$BUILD_ABI.sha256"
  )

  LAUNCH_PLAN=$(ANKINK_LAUNCHER_TEST=1 ANKINK_LAUNCHER_TEST_ABI="$BUILD_ABI" \
    "$PACKAGE_STAGE/ankink.sh")
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "ABI=$BUILD_ABI" >/dev/null
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "EXECUTABLE=$PACKAGE_STAGE/$BUILD_ABI/bin/ankinkd" >/dev/null
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "LIBRARY_PATH=$PACKAGE_STAGE/$BUILD_ABI/lib" >/dev/null
  case "$BUILD_ABI" in
    armel)
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "WHISPER_TOUCH_PRELOAD=enabled" >/dev/null
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
        "PRELOAD=$PACKAGE_STAGE/$BUILD_ABI/lib/libmesquite-whisper-touch.so" >/dev/null
      ;;
    armhf)
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "WHISPER_TOUCH_PRELOAD=disabled" >/dev/null
      printf '%s\n' "$LAUNCH_PLAN" | grep -Fx "PRELOAD=" >/dev/null
      ;;
  esac
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "ASSETS=$PACKAGE_STAGE/share/ankink" >/dev/null
  case "$BUILD_ABI" in
    armel) EXPECTED_LOADER=ld-linux.so.3 ;;
    armhf) EXPECTED_LOADER=ld-linux-armhf.so.3 ;;
  esac
  printf '%s\n' "$LAUNCH_PLAN" | grep -Fx \
    "LOADER=$PACKAGE_STAGE/$BUILD_ABI/lib/$EXPECTED_LOADER" >/dev/null
done

mv "$PACKAGE_STAGE/kual-extension/AnkINK" "$DIST_STAGE/extensions/AnkINK"
rmdir "$PACKAGE_STAGE/kual-extension"
mv "$PACKAGE_STAGE/library-launcher/AnkINK.sh" "$DIST_STAGE/documents/AnkINK.sh"
rmdir "$PACKAGE_STAGE/library-launcher"
mv "$PACKAGE_STAGE" "$DIST_STAGE/ankink"

# Do not replace a previously successful local result until every requested
# ABI has built, validated, and assembled successfully.
rm -rf "$OUTPUT/ankink" "$OUTPUT/extensions" "$OUTPUT/documents"
cp -a "$DIST_STAGE/ankink" "$OUTPUT/ankink"
cp -a "$DIST_STAGE/extensions" "$OUTPUT/extensions"
cp -a "$DIST_STAGE/documents" "$OUTPUT/documents"

echo "Built Kindle runtimes: $BUILD_ABIS"
echo "Built Kindle bundle: $OUTPUT/ankink"
echo "Built KUAL extension: $OUTPUT/extensions/AnkINK"
echo "Built Library launcher: $OUTPUT/documents/AnkINK.sh"
