#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "Usage: $0 ANKINKD ASSET_DIR SIMULATOR_ASSET_DIR STATE_DIR" >&2
  exit 2
fi

DAEMON=$1
ASSETS=$2
SIMULATOR_ASSETS=$3
STATE=$4
COLLECTION="$STATE/collection.anki2"
LOG="$STATE/ankinkd.log"
URL=http://127.0.0.1:8765/simulator/

mkdir -p "$STATE"
chmod 700 "$STATE"
if curl --silent --fail http://127.0.0.1:8765/health >/dev/null 2>&1; then
  echo "Port 8765 is already occupied by an AnkINK daemon. Stop it and run again." >&2
  exit 1
fi

"$DAEMON" --simulator --collection "$COLLECTION" --assets "$ASSETS" \
  --simulator-assets "$SIMULATOR_ASSETS" --data-dir "$STATE" >"$LOG" 2>&1 &
PID=$!
cleanup() {
  kill "$PID" >/dev/null 2>&1 || true
  wait "$PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT
trap 'exit 0' INT TERM HUP

for _ in {1..60}; do
  if curl --silent --fail http://127.0.0.1:8765/health >/dev/null 2>&1; then break; fi
  if ! kill -0 "$PID" >/dev/null 2>&1; then
    echo "AnkINK failed to start:" >&2
    tail -n 40 "$LOG" >&2
    exit 1
  fi
  sleep 0.1
done
curl --silent --fail http://127.0.0.1:8765/health >/dev/null || {
  echo "AnkINK did not become ready; see $LOG" >&2
  exit 1
}

case "$(uname -s)" in
  Darwin) open "$URL" ;;
  Linux) command -v xdg-open >/dev/null && xdg-open "$URL" >/dev/null 2>&1 || true ;;
esac
echo "AnkINK simulator: $URL"
echo "Persistent private state: $STATE"
wait "$PID"
