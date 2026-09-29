#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
IMAGE=${KINDLE_DOCKER_IMAGE:-kindle-dev-builder:local}
CACHE_VOLUME=${KINDLE_DOCKER_CACHE_VOLUME:-ankink-kindle-build-cache}
BUILD_ABI=${KINDLE_ABI:-universal}
case "$BUILD_ABI" in
  universal|all|armel|armhf) ;;
  *) echo "KINDLE_ABI must be universal, armel, or armhf." >&2; exit 2 ;;
esac
BUILD_COMMIT=${ANKINK_BUILD_COMMIT:-}
if [ -z "$BUILD_COMMIT" ] && command -v git >/dev/null 2>&1 &&
   git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  BUILD_COMMIT=$(git -C "$ROOT" rev-parse --short=12 HEAD)
  if ! git -C "$ROOT" diff-index --quiet HEAD --; then
    BUILD_COMMIT="$BUILD_COMMIT-dirty"
  fi
fi
BUILD_COMMIT=${BUILD_COMMIT:-unknown}
if [[ ! "$BUILD_COMMIT" =~ ^(unknown|[0-9a-f]{7,40}(-dirty)?)$ ]]; then
  echo "Invalid AnkINK build commit: $BUILD_COMMIT" >&2
  exit 2
fi

if [ "$#" -gt 1 ]; then
  echo "Usage: bash build_on_docker.sh [user@host]" >&2
  exit 2
fi

if [ "$#" -eq 1 ]; then
  REMOTE_TARGET=$1
  [ -n "$REMOTE_TARGET" ] || { echo "Remote SSH target must not be empty." >&2; exit 2; }
  command -v ssh >/dev/null || { echo "ssh is required for a remote build." >&2; exit 127; }
  command -v rsync >/dev/null || { echo "rsync is required for a remote build." >&2; exit 127; }

  if ! REMOTE_USER=$(ssh "$REMOTE_TARGET" '
    command -v rsync >/dev/null 2>&1 || { echo "Remote rsync is required." >&2; exit 127; }
    command -v docker >/dev/null 2>&1 || { echo "Remote Docker is required." >&2; exit 127; }
    docker info >/dev/null 2>&1 || { echo "Remote Docker is not running or is not accessible." >&2; exit 1; }
    id -un
  '); then
    echo "Remote build prerequisite check failed on $REMOTE_TARGET." >&2
    exit 1
  fi
  case "$REMOTE_USER" in
    ''|*[!A-Za-z0-9._-]*) echo "Remote user name is not safe for a build path: $REMOTE_USER" >&2; exit 1 ;;
  esac
  REMOTE_ROOT="/tmp/kindle-build-$REMOTE_USER/AnkINK"

  echo "Synchronizing AnkINK to $REMOTE_TARGET:$REMOTE_ROOT"
  ssh "$REMOTE_TARGET" "mkdir -p '$REMOTE_ROOT'"
  rsync --archive --delete \
    --exclude='.git/' \
    --exclude='dist/' \
    --exclude='target/' \
    --exclude='build/' \
    --exclude='cmake-build-*/' \
    --exclude='.ankink-simulator/' \
    --exclude-from="$ROOT/.gitignore" \
    "$ROOT/" "$REMOTE_TARGET:$REMOTE_ROOT/"

  echo "Building AnkINK with Docker on $REMOTE_TARGET"
  ssh "$REMOTE_TARGET" "cd '$REMOTE_ROOT' && ANKINK_BUILD_COMMIT='$BUILD_COMMIT' KINDLE_ABI='$BUILD_ABI' bash build_on_docker.sh"

  echo "Copying AnkINK dist back to $ROOT/dist"
  mkdir -p "$ROOT/dist"
  rsync --archive --delete "$REMOTE_TARGET:$REMOTE_ROOT/dist/" "$ROOT/dist/"
  exit 0
fi

command -v docker >/dev/null || { echo "Docker is required." >&2; exit 127; }
docker info >/dev/null || { echo "Docker is not running or is not accessible." >&2; exit 1; }

mkdir -p "$ROOT/dist"
FINGERPRINT=$( {
  cksum < "$ROOT/Dockerfile.kindle"
  cksum < "$ROOT/docker/create-sdk.sh"
  cksum < "$ROOT/docker/build-native-deps.sh"
  cksum < "$ROOT/scripts/kindle-abi.sh"
} | cksum | awk '{print $1}')
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
  --env "ANKINK_BUILD_COMMIT=$BUILD_COMMIT" \
  --env "KINDLE_ABI=$BUILD_ABI" \
  --mount "type=bind,source=$ROOT,target=/workspace,readonly" \
  --mount "type=bind,source=$ROOT/dist,target=/out" \
  --mount "type=volume,source=$CACHE_VOLUME,target=/cache" \
  "$IMAGE"
