#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 armel|armhf RUNTIME_DIRECTORY ASSET_DIRECTORY" >&2
  exit 2
fi

KINDLE_ABI=$1
RUNTIME=$2
ASSETS=$3
ANKINK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ANKINK_ROOT/scripts/kindle-abi.sh"
kindle_abi_configure "$KINDLE_ABI"

case "$KINDLE_ABI" in
  armel) PORT=19257 ;;
  armhf) PORT=19258 ;;
esac

SMOKE_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/ankink-$KINDLE_ABI-smoke.XXXXXX")
SMOKE_LOG="$SMOKE_ROOT/ankinkd.log"
SMOKE_PID=
mkdir -p "$SMOKE_ROOT/data"
cleanup() {
  if [ -n "$SMOKE_PID" ] && kill -0 "$SMOKE_PID" 2>/dev/null; then
    kill -TERM "$SMOKE_PID" 2>/dev/null || true
    wait "$SMOKE_PID" 2>/dev/null || true
  fi
  rm -rf "$SMOKE_ROOT"
}
trap cleanup EXIT HUP INT TERM

qemu-arm -r 3.0.35 "$RUNTIME/lib/$KINDLE_LOADER" \
  --library-path "$RUNTIME/lib" "$RUNTIME/bin/ankinkd" \
  --simulator \
  --collection "$SMOKE_ROOT/collection.anki2" \
  --assets "$ASSETS" \
  --simulator-assets "$ANKINK_ROOT/simulator" \
  --data-dir "$SMOKE_ROOT/data" \
  --ca-bundle "$RUNTIME/etc/ca-certificates.crt" \
  --port "$PORT" >"$SMOKE_LOG" 2>&1 &
SMOKE_PID=$!

ATTEMPT=0
while [ "$ATTEMPT" -lt 120 ]; do
  if curl -fs "http://127.0.0.1:$PORT/api/status" |
     grep -q '"type":"status"'; then
    kill -TERM "$SMOKE_PID" 2>/dev/null || true
    wait "$SMOKE_PID" 2>/dev/null || true
    SMOKE_PID=
    echo "Started $KINDLE_ABI Rust/C++ backend and reached /api/status under QEMU."
    exit 0
  fi
  if ! kill -0 "$SMOKE_PID" 2>/dev/null; then
    cat "$SMOKE_LOG" >&2
    exit 1
  fi
  ATTEMPT=$((ATTEMPT + 1))
  sleep 0.25
done

cat "$SMOKE_LOG" >&2
echo "$KINDLE_ABI backend did not become ready under QEMU" >&2
exit 1
