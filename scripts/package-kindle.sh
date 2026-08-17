#!/bin/sh
set -eu
if [ "$#" -ne 3 ]; then
  echo "Usage: $0 KINDLE_SYSROOT BUILD_DIR OUTPUT_DIRECTORY" >&2
  exit 2
fi
ANKINK_SYSROOT=$1
ANKINK_BUILD_DIR=$2
ANKINK_OUTPUT=$3
ANKINK_READELF=${CROSS_COMPILE:-arm-linux-gnueabi-}readelf
ANKINK_STRIP=${CROSS_COMPILE:-arm-linux-gnueabi-}strip

if [ ! -x "$ANKINK_BUILD_DIR/ankinkd" ]; then
  echo "Missing target executable: $ANKINK_BUILD_DIR/ankinkd" >&2; exit 1
fi
if [ -e "$ANKINK_OUTPUT" ]; then
  echo "Output already exists: $ANKINK_OUTPUT" >&2; exit 1
fi
mkdir -p "$ANKINK_OUTPUT/bin" "$ANKINK_OUTPUT/lib" "$ANKINK_OUTPUT/share/ankink" \
  "$ANKINK_OUTPUT/kual-extension/AnkINK"
cp "$ANKINK_BUILD_DIR/ankinkd" "$ANKINK_OUTPUT/bin/ankinkd"
cp assets/index.html assets/app.css assets/app.js assets/config.xml "$ANKINK_OUTPUT/share/ankink/"
cp -R assets/vendor "$ANKINK_OUTPUT/share/ankink/"
cp scripts/run-kindle.sh "$ANKINK_OUTPUT/ankink.sh"
cp scripts/stop-kindle.sh "$ANKINK_OUTPUT/stop-ankink.sh"
cp packaging/README-KINDLE.txt "$ANKINK_OUTPUT/README.txt"
cp assets/kual/config.xml assets/kual/menu.json "$ANKINK_OUTPUT/kual-extension/AnkINK/"
chmod 755 "$ANKINK_OUTPUT/bin/ankinkd" "$ANKINK_OUTPUT/ankink.sh" "$ANKINK_OUTPUT/stop-ankink.sh"

ANKINK_QUEUE=$(mktemp "${TMPDIR:-/tmp}/ankink-queue.XXXXXX")
ANKINK_SEEN=$(mktemp "${TMPDIR:-/tmp}/ankink-seen.XXXXXX")
trap 'rm -f "$ANKINK_QUEUE" "$ANKINK_SEEN"' EXIT HUP INT TERM
echo "$ANKINK_OUTPUT/bin/ankinkd" > "$ANKINK_QUEUE"

find_library() {
  ANKINK_NAME=$1
  [ -f "$ANKINK_OUTPUT/lib/$ANKINK_NAME" ] && return
  ANKINK_SOURCE=$(find "$ANKINK_SYSROOT/lib" "$ANKINK_SYSROOT/usr/lib" \
    \( -type f -o -type l \) -name "$ANKINK_NAME" 2>/dev/null | head -n 1)
  if [ -z "$ANKINK_SOURCE" ]; then echo "Missing target library: $ANKINK_NAME" >&2; exit 1; fi
  cp -L "$ANKINK_SOURCE" "$ANKINK_OUTPUT/lib/$ANKINK_NAME"
  echo "$ANKINK_OUTPUT/lib/$ANKINK_NAME" >> "$ANKINK_QUEUE"
}

ANKINK_INTERPRETER=$($ANKINK_READELF -l "$ANKINK_BUILD_DIR/ankinkd" | sed -n 's/.*interpreter: \([^]]*\)].*/\1/p')
if [ -z "$ANKINK_INTERPRETER" ]; then echo "Cannot determine dynamic loader" >&2; exit 1; fi
find_library "$(basename "$ANKINK_INTERPRETER")"

ANKINK_INDEX=1
while :; do
  ANKINK_OBJECT=$(sed -n "${ANKINK_INDEX}p" "$ANKINK_QUEUE")
  [ -n "$ANKINK_OBJECT" ] || break
  ANKINK_INDEX=$((ANKINK_INDEX + 1))
  for ANKINK_NEEDED in $($ANKINK_READELF -d "$ANKINK_OBJECT" 2>/dev/null | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p'); do
    if ! grep -Fxq "$ANKINK_NEEDED" "$ANKINK_SEEN"; then
      echo "$ANKINK_NEEDED" >> "$ANKINK_SEEN"
      find_library "$ANKINK_NEEDED"
    fi
  done
done

$ANKINK_STRIP --strip-unneeded "$ANKINK_OUTPUT/bin/ankinkd"
for ANKINK_LIBRARY in "$ANKINK_OUTPUT/lib/"*; do
  $ANKINK_STRIP --strip-unneeded "$ANKINK_LIBRARY" 2>/dev/null || true
done
echo "Minimal Mesquite bundle created at $ANKINK_OUTPUT"
echo "Copy it to /mnt/us/ankink and its KUAL extension to /mnt/us/extensions/AnkINK."
