#!/bin/sh
set -eu

ANKINK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ANKINK_RUNTIME_DIR=${TMPDIR:-/tmp}/ankink-runtime
ANKINK_PID_FILE=/var/tmp/ankink.pid
ANKINK_LOG="$ANKINK_ROOT/ankink.log"

# KUAL actions are short-lived and terminate their process group after the
# action returns. Become a new session before doing any work that must survive
# that lifetime; the detached invocation below is the actual supervisor.
if [ "${ANKINK_SUPERVISOR:-0}" != 1 ]; then
  export ANKINK_SUPERVISOR=1
  setsid "$0" "$@" </dev/null >/dev/null 2>&1 &
  exit 0
fi

if [ -f "$ANKINK_PID_FILE" ]; then
  read -r ANKINK_OLD_PID < "$ANKINK_PID_FILE" || ANKINK_OLD_PID=
  if [ -n "$ANKINK_OLD_PID" ] && kill -0 "$ANKINK_OLD_PID" 2>/dev/null; then
    echo "AnkINK is already running (PID $ANKINK_OLD_PID)" >> "$ANKINK_LOG"
    exit 0
  fi
fi
: > "$ANKINK_LOG"

ANKINK_APP_PID=
ANKINK_UI_STOPPED=0
ankink_cleanup() {
  trap - EXIT HUP INT TERM
  if [ -n "$ANKINK_APP_PID" ] && kill -0 "$ANKINK_APP_PID" 2>/dev/null; then
    kill "$ANKINK_APP_PID" 2>/dev/null || true
    sleep 1
  fi
  rm -f "$ANKINK_PID_FILE"
  if [ "$ANKINK_UI_STOPPED" -eq 1 ]; then
    /sbin/start lab126_gui >> "$ANKINK_LOG" 2>&1 || true
  fi
}
trap ankink_cleanup EXIT HUP INT TERM

mkdir -p "$ANKINK_RUNTIME_DIR"
chmod 700 "$ANKINK_RUNTIME_DIR"

export XDG_RUNTIME_DIR="$ANKINK_RUNTIME_DIR"
export XDG_DATA_DIRS="$ANKINK_ROOT/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export WEBKIT_EXEC_PATH="$ANKINK_ROOT/libexec/wpe-webkit"
if [ -d "$ANKINK_ROOT/lib/wpe-webkit/injected-bundle" ]; then
  export WEBKIT_INJECTED_BUNDLE_PATH="$ANKINK_ROOT/lib/wpe-webkit/injected-bundle"
fi
# Use the exact SONAME loaded through ankink's DT_NEEDED entry. Kindle's USB
# storage cannot preserve Unix symlinks, so a second `.so` copy would be loaded
# as a distinct DSO with a separate WPEBackend-fdo singleton.
export WPE_BACKEND_LIBRARY="$ANKINK_ROOT/lib/libWPEBackend-fdo-1.0.so.1"
export ANKINK_ASSET_DIR="$ANKINK_ROOT/share/ankink"

# The Oasis 8 has no generally usable Mesa hardware driver. Keep WebKit's EGL
# compositor, but execute it through Mesa softpipe and export CPU-readable frames.
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=softpipe
export MESA_LOADER_DRIVER_OVERRIDE=softpipe
export LIBGL_DRIVERS_PATH="$ANKINK_ROOT/lib/dri"
export EGL_PLATFORM=wayland
if [ -f "$ANKINK_ROOT/share/glvnd/egl_vendor.d/50_mesa.json" ]; then
  export __EGL_VENDOR_LIBRARY_FILENAMES="$ANKINK_ROOT/share/glvnd/egl_vendor.d/50_mesa.json"
fi
if [ -d "$ANKINK_ROOT/etc/fonts" ]; then
  export FONTCONFIG_PATH="$ANKINK_ROOT/etc/fonts"
fi
export JSC_useJIT=false
export JSC_useWasm=false
# The Kindle 3.0 kernel has neither the namespace support nor bubblewrap
# required by WPE WebKit's Linux process sandbox. AnkINK only loads its own
# local ankink:// assets, so disable that desktop-oriented sandbox explicitly.
export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1
export MALLOC_ARENA_MAX=2
export NO_AT_BRIDGE=1
export GIO_USE_VFS=local
# Mesa softpipe needs roughly fourteen bytes of working memory per rendered
# pixel. Full Oasis resolution creates a contiguous ~22 MiB mapping which the
# Kindle 3.0 ARM kernel cannot satisfy once WebKit is loaded. Render at half
# resolution by default, then let the native presenter scale the Y8 frame.
ANKINK_RENDER_SCALE=${ANKINK_RENDER_SCALE:-2}
# Mesa creates a number of worker threads. Smaller stacks keep them practical
# on the 512 MiB Oasis without affecting WebKit's non-recursive work queues.
ulimit -s 512

if [ -x "$ANKINK_ROOT/lib/ld-linux-armhf.so.3" ]; then
  ANKINK_DYNAMIC_LOADER="$ANKINK_ROOT/lib/ld-linux-armhf.so.3"
elif [ -x "$ANKINK_ROOT/lib/ld-linux.so.3" ]; then
  ANKINK_DYNAMIC_LOADER="$ANKINK_ROOT/lib/ld-linux.so.3"
else
  echo "AnkINK bundle is missing its ARM dynamic loader" >&2
  exit 1
fi
export ANKINK_DYNAMIC_LOADER
export ANKINK_PRELOAD="$ANKINK_ROOT/lib/libankink-compat.so"

if /sbin/status lab126_gui 2>/dev/null | grep -q 'start/running'; then
  /sbin/stop lab126_gui >> "$ANKINK_LOG" 2>&1
  ANKINK_UI_STOPPED=1
fi

# This helper links only FBInk and libc, so it paints before the much larger
# WPE/WebKit dependency graph is loaded.
if ! "$ANKINK_DYNAMIC_LOADER" \
  --library-path "$ANKINK_ROOT/lib" \
  --preload "$ANKINK_PRELOAD" \
  "$ANKINK_ROOT/bin/ankink-loading-screen" >> "$ANKINK_LOG" 2>&1; then
  echo "AnkINK: native loading screen failed" >> "$ANKINK_LOG"
fi

set +e
"$ANKINK_DYNAMIC_LOADER" \
  --library-path "$ANKINK_ROOT/lib" \
  --preload "$ANKINK_PRELOAD" \
  "$ANKINK_ROOT/bin/ankink-legacy-layout" \
  "$ANKINK_DYNAMIC_LOADER" \
  --library-path "$ANKINK_ROOT/lib" \
  --preload "$ANKINK_PRELOAD" \
  "$ANKINK_ROOT/bin/ankink" \
  --assets "$ANKINK_ROOT/share/ankink" \
  --collection "$ANKINK_ROOT/collection.anki2" \
  --render-scale "$ANKINK_RENDER_SCALE" \
  "$@" >> "$ANKINK_LOG" 2>&1 &
ANKINK_APP_PID=$!
echo "$ANKINK_APP_PID" > "$ANKINK_PID_FILE"
wait "$ANKINK_APP_PID"
ANKINK_RESULT=$?
ANKINK_APP_PID=
set -e
exit "$ANKINK_RESULT"
