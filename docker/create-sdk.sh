#!/bin/sh
set -eu
SDK_ROOT=/opt/kindle-sdk
. /usr/local/lib/kindle-abi.sh
mkdir -p "$SDK_ROOT/bin"

for KINDLE_SDK_ABI in armel armhf; do
  kindle_abi_configure "$KINDLE_SDK_ABI"
  mkdir -p "$SDK_ROOT/$KINDLE_ABI/usr"
  for tool in gcc g++ ar ranlib strip readelf; do
    ln -sf "/usr/bin/$KINDLE_GNU_TRIPLET-$tool" \
      "$SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-$tool"
  done
  cp -a "/usr/$KINDLE_GNU_TRIPLET/include" "$SDK_ROOT/$KINDLE_ABI/usr/"
  cp -a "/usr/$KINDLE_GNU_TRIPLET/lib" "$SDK_ROOT/$KINDLE_ABI/usr/"
  ln -sfn usr/lib "$SDK_ROOT/$KINDLE_ABI/lib"
  LIBGCC=$(find "/usr/$KINDLE_GNU_TRIPLET/lib" \
    "/usr/lib/gcc-cross/$KINDLE_GNU_TRIPLET" \
    \( -type f -o -type l \) -name libgcc_s.so.1 | head -n 1)
  [ -n "$LIBGCC" ] || {
    echo "Could not find $KINDLE_ABI libgcc_s.so.1" >&2
    exit 1
  }
  cp -L "$LIBGCC" "$SDK_ROOT/$KINDLE_ABI/usr/lib/libgcc_s.so.1"
done
