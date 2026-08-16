#!/bin/sh
set -eu

ANKINK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ANKINK_RUNTIME_DIR=${TMPDIR:-/tmp}/ankink-runtime
mkdir -p "$ANKINK_RUNTIME_DIR"
chmod 700 "$ANKINK_RUNTIME_DIR"

export XDG_RUNTIME_DIR="$ANKINK_RUNTIME_DIR"
export XDG_DATA_DIRS="$ANKINK_ROOT/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export LD_LIBRARY_PATH="$ANKINK_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WEBKIT_EXEC_PATH="$ANKINK_ROOT/libexec/wpe-webkit"
if [ -d "$ANKINK_ROOT/lib/wpe-webkit/injected-bundle" ]; then
  export WEBKIT_INJECTED_BUNDLE_PATH="$ANKINK_ROOT/lib/wpe-webkit/injected-bundle"
fi
export WPE_BACKEND_LIBRARY="$ANKINK_ROOT/lib/libWPEBackend-fdo-1.0.so"
export ANKINK_ASSET_DIR="$ANKINK_ROOT/share/ankink"

# The Oasis 8 has no generally usable Mesa hardware driver. Keep WebKit's EGL
# compositor, but execute it through Mesa softpipe and export CPU-readable frames.
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=softpipe
export MESA_LOADER_DRIVER_OVERRIDE=softpipe
export LIBGL_DRIVERS_PATH="$ANKINK_ROOT/lib/dri"
if [ -f "$ANKINK_ROOT/share/glvnd/egl_vendor.d/50_mesa.json" ]; then
  export __EGL_VENDOR_LIBRARY_FILENAMES="$ANKINK_ROOT/share/glvnd/egl_vendor.d/50_mesa.json"
fi
if [ -d "$ANKINK_ROOT/etc/fonts" ]; then
  export FONTCONFIG_PATH="$ANKINK_ROOT/etc/fonts"
fi
export JSC_useJIT=false
export JSC_useWasm=false
export MALLOC_ARENA_MAX=2
export NO_AT_BRIDGE=1
export GIO_USE_VFS=local

exec "$ANKINK_ROOT/bin/ankink" \
  --assets "$ANKINK_ROOT/share/ankink" \
  --collection "$ANKINK_ROOT/collection.anki2" \
  "$@"
