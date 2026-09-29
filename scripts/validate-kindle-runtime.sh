#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 armel|armhf RUNTIME_DIRECTORY" >&2
  exit 2
fi

KINDLE_ABI=$1
RUNTIME=$2
ANKINK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${KINDLE_SDK_ROOT:?Set KINDLE_SDK_ROOT to the SDK root}"
. "$ANKINK_ROOT/scripts/kindle-abi.sh"
kindle_abi_configure "$KINDLE_ABI"

READELF="$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-readelf"
DAEMON="$RUNTIME/bin/ankinkd"
LOADER="$RUNTIME/lib/$KINDLE_LOADER"
PRELOAD="$RUNTIME/lib/libmesquite-whisper-touch.so"

test -x "$DAEMON"
test -x "$LOADER"
test -f "$PRELOAD"

INTERPRETER=$($READELF -l "$DAEMON" | sed -n 's/.*interpreter: \([^]]*\)].*/\1/p')
if [ "$(basename "$INTERPRETER")" != "$KINDLE_LOADER" ]; then
  echo "$KINDLE_ABI ankinkd uses unexpected interpreter: $INTERPRETER" >&2
  exit 1
fi

case "$KINDLE_ABI" in
  armel) EXPECTED_FLAG='soft-float ABI'; REJECTED_FLAG='hard-float ABI' ;;
  armhf) EXPECTED_FLAG='hard-float ABI'; REJECTED_FLAG='soft-float ABI' ;;
esac

ELF_LIST=$(mktemp "${TMPDIR:-/tmp}/ankink-elf-list.XXXXXX")
trap 'rm -f "$ELF_LIST"' EXIT HUP INT TERM
find "$RUNTIME/bin" "$RUNTIME/lib" -type f -print | LC_ALL=C sort |
while IFS= read -r OBJECT; do
  if "$READELF" -h "$OBJECT" >/dev/null 2>&1; then
    printf '%s\n' "$OBJECT"
  fi
done > "$ELF_LIST"

while IFS= read -r OBJECT; do
  HEADER=$($READELF -h "$OBJECT")
  printf '%s\n' "$HEADER" | grep -q 'Machine:.*ARM' || {
    echo "Non-ARM ELF in $KINDLE_ABI runtime: $OBJECT" >&2
    exit 1
  }
  printf '%s\n' "$HEADER" | grep -q "$EXPECTED_FLAG" || {
    echo "Wrong or missing $EXPECTED_FLAG marker: $OBJECT" >&2
    exit 1
  }
  if printf '%s\n' "$HEADER" | grep -q "$REJECTED_FLAG"; then
    echo "Mixed ABI marker in $KINDLE_ABI runtime: $OBJECT" >&2
    exit 1
  fi
  for NEEDED in $($READELF -d "$OBJECT" 2>/dev/null |
    sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p'); do
    if [ ! -f "$RUNTIME/lib/$NEEDED" ]; then
      echo "Unresolved packaged dependency $NEEDED required by $OBJECT" >&2
      exit 1
    fi
  done
done < "$ELF_LIST"

if [ "$KINDLE_ABI" = armhf ]; then
  $READELF -A "$DAEMON" | grep -q 'Tag_ABI_VFP_args: VFP registers' || {
    echo "ARMHF ankinkd does not advertise VFP register arguments" >&2
    exit 1
  }
else
  if $READELF -A "$DAEMON" | grep -q 'Tag_ABI_VFP_args: VFP registers'; then
    echo "ARMEL ankinkd unexpectedly advertises VFP register arguments" >&2
    exit 1
  fi
fi

case "$KINDLE_ABI" in
  armel) test ! -e "$RUNTIME/lib/ld-linux-armhf.so.3" ;;
  armhf) test ! -e "$RUNTIME/lib/ld-linux.so.3" ;;
esac

# QEMU is only a CPU/userspace loader smoke test. It does not exercise or
# emulate Mesquite, appmgrd, e-ink, input, or any other Kindle firmware service.
qemu-arm -r 3.0.35 "$LOADER" --library-path "$RUNTIME/lib" \
  "$DAEMON" --help >/dev/null

# Exercise server startup and the statically linked Anki backend when the host
# QEMU/network environment permits it. Keep this stronger smoke advisory so an
# environment-specific QEMU limitation does not invalidate an otherwise valid
# cross-build.
if ! "$ANKINK_ROOT/scripts/smoke-kindle-backend.sh" "$KINDLE_ABI" \
  "$RUNTIME" "$ANKINK_ROOT/assets"; then
  echo "Warning: deeper $KINDLE_ABI QEMU backend smoke test was not feasible." >&2
fi

grep -a -F '/api/card-action' "$DAEMON" >/dev/null || {
  echo "Packaged ankinkd is stale: /api/card-action is missing." >&2
  exit 1
}

echo "Validated $KINDLE_ABI ELF ABI, loader, dependency closure, and QEMU loader smoke test."
