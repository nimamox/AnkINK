#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 KINDLE_SYSROOT BUILD_DIR OUTPUT_DIRECTORY" >&2
  exit 2
fi

ANKINK_SYSROOT=$1
ANKINK_BUILD_DIR=$2
ANKINK_OUTPUT=$3
ANKINK_READELF=${CROSS_COMPILE:-arm-linux-gnueabihf-}readelf
ANKINK_STRIP=${CROSS_COMPILE:-arm-linux-gnueabihf-}strip

if [ ! -x "$ANKINK_BUILD_DIR/ankink" ]; then
  echo "Missing target executable: $ANKINK_BUILD_DIR/ankink" >&2
  exit 1
fi
if [ ! -f "$ANKINK_BUILD_DIR/libankink-compat.so" ]; then
  echo "Missing compatibility library: $ANKINK_BUILD_DIR/libankink-compat.so" >&2
  exit 1
fi
if [ ! -x "$ANKINK_BUILD_DIR/ankink-legacy-layout" ]; then
  echo "Missing legacy mmap launcher: $ANKINK_BUILD_DIR/ankink-legacy-layout" >&2
  exit 1
fi
if [ ! -x "$ANKINK_BUILD_DIR/ankink-loading-screen" ]; then
  echo "Missing native loading screen: $ANKINK_BUILD_DIR/ankink-loading-screen" >&2
  exit 1
fi
if [ -e "$ANKINK_OUTPUT" ]; then
  echo "Output already exists; choose a new path: $ANKINK_OUTPUT" >&2
  exit 1
fi

mkdir -p "$ANKINK_OUTPUT/bin" "$ANKINK_OUTPUT/lib" \
  "$ANKINK_OUTPUT/libexec/wpe-webkit" "$ANKINK_OUTPUT/share/ankink" \
  "$ANKINK_OUTPUT/kual-extension/AnkINK"
cp "$ANKINK_BUILD_DIR/ankink" "$ANKINK_OUTPUT/bin/ankink"
cp "$ANKINK_BUILD_DIR/ankink-legacy-layout" \
  "$ANKINK_OUTPUT/bin/ankink-legacy-layout"
cp "$ANKINK_BUILD_DIR/ankink-loading-screen" \
  "$ANKINK_OUTPUT/bin/ankink-loading-screen"
cp "$ANKINK_BUILD_DIR/libankink-compat.so" "$ANKINK_OUTPUT/lib/libankink-compat.so"
cp scripts/run-kindle.sh "$ANKINK_OUTPUT/ankink.sh"
cp assets/index.html assets/app.css assets/app.js "$ANKINK_OUTPUT/share/ankink/"
cp assets/kual/config.xml assets/kual/menu.json "$ANKINK_OUTPUT/kual-extension/AnkINK/"
chmod 755 "$ANKINK_OUTPUT/ankink.sh" "$ANKINK_OUTPUT/bin/ankink" \
  "$ANKINK_OUTPUT/bin/ankink-legacy-layout" \
  "$ANKINK_OUTPUT/bin/ankink-loading-screen"

ANKINK_QUEUE=$(mktemp "${TMPDIR:-/tmp}/ankink-package.XXXXXX")
ANKINK_SEEN=$(mktemp "${TMPDIR:-/tmp}/ankink-seen.XXXXXX")
ANKINK_MISSING_FILE=$(mktemp "${TMPDIR:-/tmp}/ankink-missing.XXXXXX")
trap 'rm -f "$ANKINK_QUEUE" "$ANKINK_SEEN" "$ANKINK_MISSING_FILE"' EXIT HUP INT TERM

find_program() {
  ANKINK_CANDIDATE=$(find "$ANKINK_SYSROOT/usr" -type f -name "$1" 2>/dev/null | head -n 1)
  if [ -n "$ANKINK_CANDIDATE" ] && [ -x "$ANKINK_CANDIDATE" ]; then
    cp "$ANKINK_CANDIDATE" "$ANKINK_OUTPUT/libexec/wpe-webkit/$1.bin"
    cp scripts/run-wpe-helper.sh "$ANKINK_OUTPUT/libexec/wpe-webkit/$1"
    chmod 755 "$ANKINK_OUTPUT/libexec/wpe-webkit/$1" \
      "$ANKINK_OUTPUT/libexec/wpe-webkit/$1.bin"
    echo "$ANKINK_OUTPUT/libexec/wpe-webkit/$1.bin" >> "$ANKINK_QUEUE"
    return
  fi
  echo "Could not find WPE helper $1 in the sysroot" >&2
  exit 1
}

find_library() {
  if [ -f "$ANKINK_OUTPUT/lib/$1" ] || [ -L "$ANKINK_OUTPUT/lib/$1" ]; then
    echo "$ANKINK_OUTPUT/lib/$1" >> "$ANKINK_QUEUE"
    return
  fi
  ANKINK_LIBRARY=$(find \
    "$ANKINK_SYSROOT/lib" "$ANKINK_SYSROOT/usr/lib" \
    \( -type f -o -type l \) -name "$1" 2>/dev/null |
    while IFS= read -r ANKINK_CANDIDATE; do
      if "$ANKINK_READELF" -h "$ANKINK_CANDIDATE" 2>/dev/null |
           grep -q 'Machine:.*ARM'; then
        echo "$ANKINK_CANDIDATE"
        break
      fi
    done)
  if [ -z "$ANKINK_LIBRARY" ]; then
    echo "Missing target library: $1" >&2
    exit 1
  fi
  cp -L "$ANKINK_LIBRARY" "$ANKINK_OUTPUT/lib/$1"
  echo "$ANKINK_OUTPUT/lib/$1" >> "$ANKINK_QUEUE"
}

find_program WPEWebProcess
find_program WPENetworkProcess
find_program WPEGPUProcess

ANKINK_INJECTED=$(find "$ANKINK_SYSROOT/usr" -type f \
  \( -name 'libWPEInjectedBundle.so*' -o \
     -name 'libWPEWebKitInjectedBundle.so*' \) 2>/dev/null | head -n 1)
if [ -n "$ANKINK_INJECTED" ]; then
  mkdir -p "$ANKINK_OUTPUT/lib/wpe-webkit/injected-bundle"
  ANKINK_INJECTED_NAME=$(basename "$ANKINK_INJECTED")
  cp -L "$ANKINK_INJECTED" \
    "$ANKINK_OUTPUT/lib/wpe-webkit/injected-bundle/$ANKINK_INJECTED_NAME"
  echo "$ANKINK_OUTPUT/lib/wpe-webkit/injected-bundle/$ANKINK_INJECTED_NAME" >> "$ANKINK_QUEUE"
fi

# Prefer the Kindle-compatible backend rebuilt with the static loader patch.
# Do not create a second `.so` name: Kindle USB storage is FAT and cannot
# preserve symlinks, and loading two copies creates two backend singletons.
ANKINK_WPE_FDO_ROOT=${ANKINK_WPE_FDO_ROOT:-}
if [ -z "$ANKINK_WPE_FDO_ROOT" ] &&
   [ "$(basename "$ANKINK_SYSROOT")" = armel ]; then
  ANKINK_WPE_FDO_ROOT="$(dirname "$ANKINK_SYSROOT")/wpebackend-fdo-armel-static-loader"
fi
if [ -n "$ANKINK_WPE_FDO_ROOT" ] &&
   [ -f "$ANKINK_WPE_FDO_ROOT/lib/libWPEBackend-fdo-1.0.so.1.10.1" ]; then
  cp -L "$ANKINK_WPE_FDO_ROOT/lib/libWPEBackend-fdo-1.0.so.1.10.1" \
    "$ANKINK_OUTPUT/lib/libWPEBackend-fdo-1.0.so.1"
  echo "$ANKINK_OUTPUT/lib/libWPEBackend-fdo-1.0.so.1" >> "$ANKINK_QUEUE"
elif [ "$(basename "$ANKINK_SYSROOT")" = armel ]; then
  echo "Missing Kindle-compatible ARMEL WPEBackend-fdo in $ANKINK_WPE_FDO_ROOT" >&2
  exit 1
else
  find_library libWPEBackend-fdo-1.0.so.1
fi

# GLVND and Mesa load these by name at runtime, so they do not appear in the
# executable's DT_NEEDED closure. Prefer AnkINK's ARMv7-specific softpipe Mesa:
# Debian's generic ARM build contains GCC's __kernel_cmpxchg64 fallback, which
# aborts on the Kindle's Linux 3.0 kernel even though its ARMv7 CPU has native
# LDREXD/STREXD instructions.
ANKINK_MESA_ROOT=${ANKINK_MESA_ROOT:-"$(dirname "$ANKINK_SYSROOT")/mesa-armel-install"}
if [ -f "$ANKINK_MESA_ROOT/lib/libEGL_mesa.so.0" ] &&
   [ -f "$ANKINK_MESA_ROOT/lib/libgallium-25.0.7.so" ]; then
  cp -L "$ANKINK_MESA_ROOT/lib/libEGL_mesa.so.0" \
    "$ANKINK_OUTPUT/lib/libEGL_mesa.so.0"
  cp -L "$ANKINK_MESA_ROOT/lib/libgbm.so.1" \
    "$ANKINK_OUTPUT/lib/libgbm.so.1"
  cp -L "$ANKINK_MESA_ROOT/lib/libexpat.so.1" \
    "$ANKINK_OUTPUT/lib/libexpat.so.1"
  cp "$ANKINK_MESA_ROOT/lib/libgallium-25.0.7.so" \
    "$ANKINK_OUTPUT/lib/libgallium-25.0.7.so"
  echo "$ANKINK_OUTPUT/lib/libEGL_mesa.so.0" >> "$ANKINK_QUEUE"
  echo "$ANKINK_OUTPUT/lib/libgbm.so.1" >> "$ANKINK_QUEUE"
  echo "$ANKINK_OUTPUT/lib/libexpat.so.1" >> "$ANKINK_QUEUE"
  echo "$ANKINK_OUTPUT/lib/libgallium-25.0.7.so" >> "$ANKINK_QUEUE"

  mkdir -p "$ANKINK_OUTPUT/lib/dri"
  cp "$ANKINK_MESA_ROOT/lib/libgallium-25.0.7.so" \
    "$ANKINK_OUTPUT/lib/dri/swrast_dri.so"
  "$ANKINK_STRIP" --strip-unneeded \
    "$ANKINK_OUTPUT/lib/libEGL_mesa.so.0" \
    "$ANKINK_OUTPUT/lib/libgbm.so.1" \
    "$ANKINK_OUTPUT/lib/libexpat.so.1" \
    "$ANKINK_OUTPUT/lib/libgallium-25.0.7.so" \
    "$ANKINK_OUTPUT/lib/dri/swrast_dri.so"
  echo "$ANKINK_OUTPUT/lib/dri/swrast_dri.so" >> "$ANKINK_QUEUE"
else
  find_library libEGL_mesa.so.0
  ANKINK_GALLIUM=$(find "$ANKINK_SYSROOT/usr/lib" "$ANKINK_SYSROOT/lib" \
    \( -type f -o -type l \) -name 'libgallium*.so*' 2>/dev/null | head -n 1)
  if [ -z "$ANKINK_GALLIUM" ]; then
    echo "Missing Mesa Gallium runtime in the sysroot" >&2
    exit 1
  fi
  find_library "$(basename "$ANKINK_GALLIUM")"

  ANKINK_SWRAST=$(find "$ANKINK_SYSROOT/usr/lib" "$ANKINK_SYSROOT/lib" \
    \( -type f -o -type l \) -name swrast_dri.so 2>/dev/null | head -n 1)
  if [ -z "$ANKINK_SWRAST" ]; then
    echo "Missing Mesa softpipe driver (swrast_dri.so) in the sysroot" >&2
    exit 1
  fi
  mkdir -p "$ANKINK_OUTPUT/lib/dri"
  cp -L "$ANKINK_SWRAST" "$ANKINK_OUTPUT/lib/dri/swrast_dri.so"
  echo "$ANKINK_OUTPUT/lib/dri/swrast_dri.so" >> "$ANKINK_QUEUE"
fi
find_library libGLESv2.so.2

ANKINK_INTERPRETER=$(
  "$ANKINK_READELF" -l "$ANKINK_BUILD_DIR/ankink" |
    sed -n 's/.*interpreter: \([^]]*\)].*/\1/p'
)
if [ -z "$ANKINK_INTERPRETER" ]; then
  echo "Could not determine the target dynamic loader" >&2
  exit 1
fi
find_library "$(basename "$ANKINK_INTERPRETER")"

if [ -f "$ANKINK_MESA_ROOT/share/glvnd/egl_vendor.d/50_mesa.json" ]; then
  mkdir -p "$ANKINK_OUTPUT/share/glvnd/egl_vendor.d"
  cp "$ANKINK_MESA_ROOT/share/glvnd/egl_vendor.d/50_mesa.json" \
    "$ANKINK_OUTPUT/share/glvnd/egl_vendor.d/50_mesa.json"
elif [ -f "$ANKINK_SYSROOT/usr/share/glvnd/egl_vendor.d/50_mesa.json" ]; then
  mkdir -p "$ANKINK_OUTPUT/share/glvnd/egl_vendor.d"
  cp "$ANKINK_SYSROOT/usr/share/glvnd/egl_vendor.d/50_mesa.json" \
    "$ANKINK_OUTPUT/share/glvnd/egl_vendor.d/50_mesa.json"
fi
if [ -d "$ANKINK_SYSROOT/etc/fonts" ]; then
  mkdir -p "$ANKINK_OUTPUT/etc/fonts/conf.avail" \
    "$ANKINK_OUTPUT/etc/fonts/conf.d"
  cp "$ANKINK_SYSROOT/etc/fonts/fonts.conf" "$ANKINK_OUTPUT/etc/fonts/"
  find "$ANKINK_SYSROOT/etc/fonts/conf.avail" -maxdepth 1 -type f |
    while IFS= read -r ANKINK_FONT_CONFIG; do
      cp "$ANKINK_FONT_CONFIG" "$ANKINK_OUTPUT/etc/fonts/conf.avail/"
    done
  for ANKINK_FONT_LINK in "$ANKINK_SYSROOT/etc/fonts/conf.d/"*; do
    ANKINK_FONT_NAME=$(basename "$ANKINK_FONT_LINK")
    if [ ! -L "$ANKINK_FONT_LINK" ] && [ -f "$ANKINK_FONT_LINK" ]; then
      cp "$ANKINK_FONT_LINK" "$ANKINK_OUTPUT/etc/fonts/conf.d/$ANKINK_FONT_NAME"
    elif [ -f "$ANKINK_SYSROOT/etc/fonts/conf.avail/$ANKINK_FONT_NAME" ]; then
      cp "$ANKINK_SYSROOT/etc/fonts/conf.avail/$ANKINK_FONT_NAME" \
        "$ANKINK_OUTPUT/etc/fonts/conf.d/$ANKINK_FONT_NAME"
    elif [ -f "$ANKINK_SYSROOT/usr/share/fontconfig/conf.avail/$ANKINK_FONT_NAME" ]; then
      cp "$ANKINK_SYSROOT/usr/share/fontconfig/conf.avail/$ANKINK_FONT_NAME" \
        "$ANKINK_OUTPUT/etc/fonts/conf.d/$ANKINK_FONT_NAME"
    else
      echo "Unresolved Fontconfig entry: $ANKINK_FONT_LINK" >&2
      exit 1
    fi
  done
fi
if [ -d "$ANKINK_SYSROOT/usr/share/fonts" ]; then
  mkdir -p "$ANKINK_OUTPUT/share/fonts"
  cp -RL "$ANKINK_SYSROOT/usr/share/fonts/." "$ANKINK_OUTPUT/share/fonts/"
fi

echo "$ANKINK_OUTPUT/bin/ankink" >> "$ANKINK_QUEUE"
echo "$ANKINK_OUTPUT/bin/ankink-legacy-layout" >> "$ANKINK_QUEUE"
echo "$ANKINK_OUTPUT/bin/ankink-loading-screen" >> "$ANKINK_QUEUE"
echo "$ANKINK_OUTPUT/lib/libankink-compat.so" >> "$ANKINK_QUEUE"
ANKINK_QUEUE_INDEX=1
while :; do
  ANKINK_OBJECT=$(sed -n "${ANKINK_QUEUE_INDEX}p" "$ANKINK_QUEUE")
  if [ -z "$ANKINK_OBJECT" ]; then
    break
  fi
  ANKINK_QUEUE_INDEX=$((ANKINK_QUEUE_INDEX + 1))
  ANKINK_NEEDED_LIST=$("$ANKINK_READELF" -d "$ANKINK_OBJECT" 2>/dev/null |
    sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
  for ANKINK_NEEDED in $ANKINK_NEEDED_LIST; do
    if ! grep -Fxq "$ANKINK_NEEDED" "$ANKINK_SEEN"; then
      echo "$ANKINK_NEEDED" >> "$ANKINK_SEEN"
      find_library "$ANKINK_NEEDED"
    fi
  done
done

# Validate every ELF object in the finished tree, not merely executables. This
# catches missing dependencies of WebKit helpers, Mesa drivers and libraries
# reached through dlopen().
find "$ANKINK_OUTPUT" -type f | while IFS= read -r ANKINK_OBJECT; do
  if "$ANKINK_READELF" -h "$ANKINK_OBJECT" >/dev/null 2>&1; then
    ANKINK_NEEDED_LIST=$("$ANKINK_READELF" -d "$ANKINK_OBJECT" 2>/dev/null |
      sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
    for ANKINK_NEEDED in $ANKINK_NEEDED_LIST; do
      if [ ! -f "$ANKINK_OUTPUT/lib/$ANKINK_NEEDED" ] &&
         [ ! -L "$ANKINK_OUTPUT/lib/$ANKINK_NEEDED" ]; then
        echo "Unresolved bundled dependency: $ANKINK_NEEDED (from $ANKINK_OBJECT)" >&2
        echo "$ANKINK_NEEDED" >> "$ANKINK_MISSING_FILE"
      fi
    done
  fi
done
if [ -s "$ANKINK_MISSING_FILE" ]; then
  exit 1
fi

find "$ANKINK_OUTPUT" -type f | while IFS= read -r ANKINK_OBJECT; do
  if grep -aFq 'A newer kernel is required to run this binary. (__kernel_cmpxchg64 helper)' \
       "$ANKINK_OBJECT"; then
    echo "Kernel-incompatible 64-bit ARM atomic fallback in $ANKINK_OBJECT" >&2
    echo "$ANKINK_OBJECT" >> "$ANKINK_MISSING_FILE"
  fi
done
if [ -s "$ANKINK_MISSING_FILE" ]; then
  exit 1
fi

# Debian's WPE WebKit packages compile the subprocess and injected-bundle
# directories into libWPEWebKit and do not honor WEBKIT_EXEC_PATH. The Kindle
# bundle has no safe way to create that Debian directory below the read-only
# root filesystem, so rewrite the fixed-length strings to the documented
# deployment root. Replacements are padded with NUL bytes and must not be
# longer than the strings they replace.
ANKINK_WEBKIT_LIBRARY="$ANKINK_OUTPUT/lib/libWPEWebKit-2.0.so.1"
if [ -f "$ANKINK_WEBKIT_LIBRARY" ]; then
  patch_webkit_path() {
    ANKINK_PATCH_OLD=$1 ANKINK_PATCH_NEW=$2 perl -0777 -pi -e '
      BEGIN {
        $old = $ENV{"ANKINK_PATCH_OLD"};
        $new = $ENV{"ANKINK_PATCH_NEW"};
        die "replacement path is too long\n" if length($new) > length($old);
        $replacement = $new . ("\0" x (length($old) - length($new)));
      }
      $count += s/\Q$old\E/$replacement/g;
      END { die "compiled WebKit path not found: $old\n" unless $count; }
    ' "$ANKINK_WEBKIT_LIBRARY"
  }
  patch_webkit_path \
    /usr/lib/arm-linux-gnueabi/wpe-webkit-2.0/injected-bundle/ \
    /mnt/us/ankink/lib/wpe-webkit/injected-bundle/
  patch_webkit_path \
    /usr/lib/arm-linux-gnueabi/wpe-webkit-2.0 \
    /mnt/us/ankink/libexec/wpe-webkit
fi

echo "Bundle created at $ANKINK_OUTPUT"
echo "Copy collection.anki2 into that directory, then copy the directory to /mnt/us/ankink on the Kindle."
echo "Copy $ANKINK_OUTPUT/kual-extension/AnkINK to /mnt/us/extensions/AnkINK to add the KUAL launcher."
