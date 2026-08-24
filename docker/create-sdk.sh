#!/bin/sh
set -eu
SDK_ROOT=/opt/kindle-sdk
TRIPLET=arm-linux-gnueabi
mkdir -p "$SDK_ROOT/bin" "$SDK_ROOT/armel/usr"
for tool in gcc g++ ar ranlib strip readelf; do ln -sf "/usr/bin/$TRIPLET-$tool" "$SDK_ROOT/bin/$TRIPLET-$tool"; done
cp -a "/usr/$TRIPLET/include" "$SDK_ROOT/armel/usr/"
cp -a "/usr/$TRIPLET/lib" "$SDK_ROOT/armel/usr/"
ln -sfn usr/lib "$SDK_ROOT/armel/lib"
LIBGCC=$(find "/usr/$TRIPLET/lib" "/usr/lib/gcc-cross/$TRIPLET" \( -type f -o -type l \) -name libgcc_s.so.1 | head -n 1)
[ -n "$LIBGCC" ] || { echo "Could not find ARMEL libgcc_s.so.1" >&2; exit 1; }
cp -L "$LIBGCC" "$SDK_ROOT/armel/usr/lib/libgcc_s.so.1"
