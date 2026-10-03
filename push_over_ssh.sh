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
LIBRARY_LAUNCHER="$ROOT/dist/documents/AnkINK.sh"

for ABI in armel armhf; do
  test -x "$BUNDLE/$ABI/bin/ankinkd" || {
    echo "Missing $BUNDLE/$ABI/bin/ankinkd. Run the universal ./build_on_docker.sh first." >&2
    exit 1
  }
done
test -f "$EXTENSION/config.xml" || {
  echo "Missing KUAL extension. Run ./build_on_docker.sh first." >&2
  exit 1
}
test -x "$LIBRARY_LAUNCHER" || {
  echo "Missing Library launcher. Run ./build_on_docker.sh first." >&2
  exit 1
}

ssh "$TARGET" 'mkdir -p /mnt/us/ankink /mnt/us/extensions/AnkINK /mnt/us/documents'
if command -v rsync >/dev/null && ssh "$TARGET" 'command -v rsync >/dev/null 2>&1'; then
  # /mnt/us is Kindle's user-storage mount and does not support chown.
  # Archive mode normally implies --owner and --group, so turn them off.
  rsync -az --delete --no-owner --no-group "$BUNDLE/" "$TARGET:/mnt/us/ankink/"
  rsync -az --delete --no-owner --no-group "$EXTENSION/" "$TARGET:/mnt/us/extensions/AnkINK/"
  rsync -az --no-owner --no-group "$LIBRARY_LAUNCHER" "$TARGET:/mnt/us/documents/AnkINK.sh"
else
  # Copy the contents explicitly: older SCP servers reject a directory named '.'.
  shopt -s dotglob
  scp -pr "$BUNDLE/"* "$TARGET:/mnt/us/ankink/"
  scp -pr "$EXTENSION/"* "$TARGET:/mnt/us/extensions/AnkINK/"
  scp -p "$LIBRARY_LAUNCHER" "$TARGET:/mnt/us/documents/AnkINK.sh"
fi

echo "Installed AnkINK at $TARGET:/mnt/us/ankink"
echo "Installed KUAL extension at $TARGET:/mnt/us/extensions/AnkINK"
echo "Installed Library launcher at $TARGET:/mnt/us/documents/AnkINK.sh"
echo "Relaunch AnkINK from the Library or KUAL to load the new Mesquite assets."
