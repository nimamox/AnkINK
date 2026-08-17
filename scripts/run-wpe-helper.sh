#!/bin/sh

set -eu

ANKINK_HELPER_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ANKINK_ROOT=$(CDPATH= cd -- "$ANKINK_HELPER_DIR/../.." && pwd)
ANKINK_HELPER=$(basename -- "$0")

: "${ANKINK_DYNAMIC_LOADER:?AnkINK launcher did not select a bundled dynamic loader}"
: "${ANKINK_PRELOAD:?AnkINK launcher did not select its compatibility library}"

exec "$ANKINK_DYNAMIC_LOADER" \
  --library-path "$ANKINK_ROOT/lib" \
  --preload "$ANKINK_PRELOAD" \
  "$ANKINK_HELPER_DIR/$ANKINK_HELPER.bin" "$@"
