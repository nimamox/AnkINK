#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 KINDLE_SYSROOT BUILD_DIR OUTPUT_DIRECTORY" >&2
  exit 2
fi

ANKINK_SYSROOT=$1
ANKINK_BUILD_DIR=$2
ANKINK_OUTPUT=$3
ANKINK_READELF=${CROSS_COMPILE:-arm-linux-gnueabihf-}readelf

if [ ! -x "$ANKINK_BUILD_DIR/ankink" ]; then
  echo "Missing target executable: $ANKINK_BUILD_DIR/ankink" >&2
  exit 1
fi
if [ -e "$ANKINK_OUTPUT" ]; then
  echo "Output already exists; choose a new path: $ANKINK_OUTPUT" >&2
  exit 1
fi

mkdir -p "$ANKINK_OUTPUT/bin" "$ANKINK_OUTPUT/lib" \
  "$ANKINK_OUTPUT/libexec/wpe-webkit" "$ANKINK_OUTPUT/share/ankink"
cp "$ANKINK_BUILD_DIR/ankink" "$ANKINK_OUTPUT/bin/ankink"
cp scripts/run-kindle.sh "$ANKINK_OUTPUT/ankink.sh"
cp assets/index.html assets/app.css assets/app.js "$ANKINK_OUTPUT/share/ankink/"
chmod 755 "$ANKINK_OUTPUT/ankink.sh" "$ANKINK_OUTPUT/bin/ankink"

ANKINK_QUEUE=$(mktemp "${TMPDIR:-/tmp}/ankink-package.XXXXXX")
ANKINK_SEEN=$(mktemp "${TMPDIR:-/tmp}/ankink-seen.XXXXXX")
trap 'rm -f "$ANKINK_QUEUE" "$ANKINK_SEEN"' EXIT HUP INT TERM

find_program() {
  ANKINK_CANDIDATE=$(find "$ANKINK_SYSROOT/usr" -type f -name "$1" 2>/dev/null | head -n 1)
  if [ -n "$ANKINK_CANDIDATE" ] && [ -x "$ANKINK_CANDIDATE" ]; then
    cp "$ANKINK_CANDIDATE" "$ANKINK_OUTPUT/libexec/wpe-webkit/$1"
    echo "$ANKINK_OUTPUT/libexec/wpe-webkit/$1" >> "$ANKINK_QUEUE"
    return
  fi
  echo "Could not find WPE helper $1 in the sysroot" >&2
  exit 1
}

find_library() {
  ANKINK_LIBRARY=$(find \
    "$ANKINK_SYSROOT/lib" "$ANKINK_SYSROOT/usr/lib" \
    -type f -o -type l 2>/dev/null | while IFS= read -r ANKINK_CANDIDATE; do
      if [ "$(basename "$ANKINK_CANDIDATE")" = "$1" ]; then
        echo "$ANKINK_CANDIDATE"
        break
      fi
    done)
  if [ -z "$ANKINK_LIBRARY" ]; then
    echo "Missing target library: $1" >&2
    exit 1
  fi
  cp -L "$ANKINK_LIBRARY" "$ANKINK_OUTPUT/lib/$1"
  echo "$ANKINK_OUTPUT/lib/$1" >> "$ANKINK_QUEUE"
}

find_program WPEWebProcess
find_program WPENetworkProcess

ANKINK_INJECTED=$(find "$ANKINK_SYSROOT/usr" -type f -name 'libWPEWebKitInjectedBundle.so*' 2>/dev/null | head -n 1)
if [ -n "$ANKINK_INJECTED" ]; then
  mkdir -p "$ANKINK_OUTPUT/lib/wpe-webkit/injected-bundle"
  cp -L "$ANKINK_INJECTED" "$ANKINK_OUTPUT/lib/wpe-webkit/injected-bundle/libWPEWebKitInjectedBundle.so"
  echo "$ANKINK_OUTPUT/lib/wpe-webkit/injected-bundle/libWPEWebKitInjectedBundle.so" >> "$ANKINK_QUEUE"
fi

for ANKINK_BACKEND in \
  "$ANKINK_SYSROOT/usr/lib/libWPEBackend-fdo-1.0.so" \
  "$ANKINK_SYSROOT/usr/lib/libWPEBackend-fdo-1.0.so.1"; do
  if [ -e "$ANKINK_BACKEND" ]; then
    cp -L "$ANKINK_BACKEND" "$ANKINK_OUTPUT/lib/libWPEBackend-fdo-1.0.so"
    echo "$ANKINK_OUTPUT/lib/libWPEBackend-fdo-1.0.so" >> "$ANKINK_QUEUE"
    break
  fi
done
if [ ! -e "$ANKINK_OUTPUT/lib/libWPEBackend-fdo-1.0.so" ]; then
  echo "Missing WPEBackend-fdo in the sysroot" >&2
  exit 1
fi

ANKINK_SWRAST=$(find "$ANKINK_SYSROOT/usr/lib" "$ANKINK_SYSROOT/lib" -name swrast_dri.so -type f 2>/dev/null | head -n 1)
if [ -z "$ANKINK_SWRAST" ]; then
  echo "Missing Mesa softpipe driver (swrast_dri.so) in the sysroot" >&2
  exit 1
fi
mkdir -p "$ANKINK_OUTPUT/lib/dri"
cp -L "$ANKINK_SWRAST" "$ANKINK_OUTPUT/lib/dri/swrast_dri.so"
echo "$ANKINK_OUTPUT/lib/dri/swrast_dri.so" >> "$ANKINK_QUEUE"

if [ -f "$ANKINK_SYSROOT/usr/share/glvnd/egl_vendor.d/50_mesa.json" ]; then
  mkdir -p "$ANKINK_OUTPUT/share/glvnd/egl_vendor.d"
  cp "$ANKINK_SYSROOT/usr/share/glvnd/egl_vendor.d/50_mesa.json" \
    "$ANKINK_OUTPUT/share/glvnd/egl_vendor.d/50_mesa.json"
fi
if [ -d "$ANKINK_SYSROOT/etc/fonts" ]; then
  mkdir -p "$ANKINK_OUTPUT/etc"
  cp -R "$ANKINK_SYSROOT/etc/fonts" "$ANKINK_OUTPUT/etc/fonts"
fi
if [ -d "$ANKINK_SYSROOT/usr/share/fonts" ]; then
  mkdir -p "$ANKINK_OUTPUT/share"
  cp -R "$ANKINK_SYSROOT/usr/share/fonts" "$ANKINK_OUTPUT/share/fonts"
fi

echo "$ANKINK_OUTPUT/bin/ankink" >> "$ANKINK_QUEUE"
while IFS= read -r ANKINK_OBJECT; do
  "$ANKINK_READELF" -d "$ANKINK_OBJECT" 2>/dev/null |
    sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' |
    while IFS= read -r ANKINK_NEEDED; do
      if ! grep -Fxq "$ANKINK_NEEDED" "$ANKINK_SEEN"; then
        echo "$ANKINK_NEEDED" >> "$ANKINK_SEEN"
        find_library "$ANKINK_NEEDED"
      fi
    done
done < "$ANKINK_QUEUE"

echo "Bundle created at $ANKINK_OUTPUT"
echo "Copy collection.anki2 into that directory, then copy the directory to /mnt/us/ankink on the Kindle."
