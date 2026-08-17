#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
IMAGE=${ANKINK_DOCKER_IMAGE:-ankink-kindle-builder:local}
CACHE_VOLUME=${ANKINK_DOCKER_CACHE_VOLUME:-ankink-kindle-build-cache}

command -v docker >/dev/null || { echo "Docker is required." >&2; exit 127; }
docker info >/dev/null || { echo "Docker is not running or is not accessible." >&2; exit 1; }

mkdir -p "$ROOT/dist"
docker build --file "$ROOT/Dockerfile.kindle" --tag "$IMAGE" "$ROOT"
docker volume create "$CACHE_VOLUME" >/dev/null

docker run --rm \
  --env "HOST_UID=$(id -u)" \
  --env "HOST_GID=$(id -g)" \
  --mount "type=bind,source=$ROOT,target=/workspace,readonly" \
  --mount "type=bind,source=$ROOT/dist,target=/out" \
  --mount "type=volume,source=$CACHE_VOLUME,target=/cache" \
  "$IMAGE"
