#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
IMAGE=${KINDLE_DOCKER_IMAGE:-kindle-dev-builder:local}
CACHE_VOLUME=${KINDLE_DOCKER_CACHE_VOLUME:-ankink-kindle-build-cache}

command -v docker >/dev/null || { echo "Docker is required." >&2; exit 127; }
docker info >/dev/null || { echo "Docker is not running or is not accessible." >&2; exit 1; }

mkdir -p "$ROOT/dist"
FINGERPRINT=$( { cksum < "$ROOT/Dockerfile.kindle"; cksum < "$ROOT/docker/create-sdk.sh"; } | cksum | awk '{print $1}')
FINGERPRINT_IMAGE="kindle-dev-builder:cache-$FINGERPRINT"
if docker image inspect "$FINGERPRINT_IMAGE" >/dev/null 2>&1; then
  docker tag "$FINGERPRINT_IMAGE" "$IMAGE"
else
  docker build --file "$ROOT/Dockerfile.kindle" --tag "$IMAGE" --tag "$FINGERPRINT_IMAGE" "$ROOT"
fi
docker image rm ankink-kindle-builder:local >/dev/null 2>&1 || true
for OLD_IMAGE in $(docker images kindle-dev-builder --format '{{.Repository}}:{{.Tag}}'); do
  case "$OLD_IMAGE" in "$IMAGE"|"$FINGERPRINT_IMAGE") ;; *) docker image rm "$OLD_IMAGE" >/dev/null 2>&1 || true ;; esac
done
docker volume create "$CACHE_VOLUME" >/dev/null

docker run --rm \
  --env "HOST_UID=$(id -u)" \
  --env "HOST_GID=$(id -g)" \
  --mount "type=bind,source=$ROOT,target=/workspace,readonly" \
  --mount "type=bind,source=$ROOT/dist,target=/out" \
  --mount "type=volume,source=$CACHE_VOLUME,target=/cache" \
  "$IMAGE"
