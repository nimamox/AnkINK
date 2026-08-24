#!/bin/sh
set -eu

HOST_UID=${HOST_UID:-0}
HOST_GID=${HOST_GID:-0}
mkdir -p /cache /out
chown -R "$HOST_UID:$HOST_GID" /cache
chown "$HOST_UID:$HOST_GID" /out
mkdir -p /opt/anki/out
chown "$HOST_UID:$HOST_GID" /opt/anki/out
export HOME=/tmp/kindle-build-user
mkdir -p "$HOME"
chown "$HOST_UID:$HOST_GID" "$HOME"

if [ "$HOST_UID" = 0 ]; then
  exec /bin/sh /workspace/docker/build-kindle.sh
fi
exec setpriv --reuid="$HOST_UID" --regid="$HOST_GID" --clear-groups \
  /bin/sh /workspace/docker/build-kindle.sh
