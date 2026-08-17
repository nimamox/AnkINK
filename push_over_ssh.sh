#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 USER@KINDLE_HOST" >&2
  exit 2
fi

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
TARGET=$1
BUNDLE="$ROOT/dist/ankink"
EXTENSION="$ROOT/dist/extensions/AnkINK"

test -x "$BUNDLE/bin/ankinkd" || {
  echo "Missing $BUNDLE/bin/ankinkd. Run ./build_on_docker.sh first." >&2
  exit 1
}
test -f "$EXTENSION/config.xml" || {
  echo "Missing KUAL extension. Run ./build_on_docker.sh first." >&2
  exit 1
}

ssh "$TARGET" 'mkdir -p /mnt/us/ankink /mnt/us/extensions/AnkINK'
if command -v rsync >/dev/null; then
  # /mnt/us is Kindle's user-storage mount and does not support chown.
  # Archive mode normally implies --owner and --group, so turn them off.
  rsync -az --delete --no-owner --no-group "$BUNDLE/" "$TARGET:/mnt/us/ankink/"
  rsync -az --delete --no-owner --no-group "$EXTENSION/" "$TARGET:/mnt/us/extensions/AnkINK/"
else
  scp -pr "$BUNDLE/." "$TARGET:/mnt/us/ankink/"
  scp -pr "$EXTENSION/." "$TARGET:/mnt/us/extensions/AnkINK/"
fi

echo "Installed AnkINK at $TARGET:/mnt/us/ankink"
echo "Installed KUAL extension at $TARGET:/mnt/us/extensions/AnkINK"
echo "Relaunch AnkINK from KUAL to load the new Mesquite assets."
