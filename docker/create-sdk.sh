#!/bin/sh
set -eu

SDK_ROOT=/opt/ankink-sdk
TRIPLET=arm-linux-gnueabi

mkdir -p "$SDK_ROOT/bin" "$SDK_ROOT/armel/usr"
for tool in gcc g++ ar ranlib strip readelf; do
  ln -s "/usr/bin/$TRIPLET-$tool" "$SDK_ROOT/bin/$TRIPLET-$tool"
done

# Debian's cross sysroot is made concrete here because package-kindle.sh scans
# the sysroot to copy the runtime closure into the Kindle bundle.
cp -a "/usr/$TRIPLET/include" "$SDK_ROOT/armel/usr/"
cp -a "/usr/$TRIPLET/lib" "$SDK_ROOT/armel/usr/"
ln -s usr/lib "$SDK_ROOT/armel/lib"

LIBGCC=$(find "/usr/$TRIPLET/lib" "/usr/lib/gcc-cross/$TRIPLET" \
  \( -type f -o -type l \) -name libgcc_s.so.1 | head -n 1)
if [ -z "$LIBGCC" ]; then
  echo "Could not find ARMEL libgcc_s.so.1" >&2
  exit 1
fi
cp -L "$LIBGCC" "$SDK_ROOT/armel/usr/lib/libgcc_s.so.1"
