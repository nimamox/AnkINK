#!/bin/sh
set -eu

ANKINK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ANKINK_APP_ID=org.ankink.app
ANKINK_APPREG=/var/local/appreg.db
ANKINK_PID_FILE=/var/tmp/ankinkd.pid
ANKINK_LOG="$ANKINK_ROOT/ankinkd.log"
ANKINK_DATA_DIR=/var/local/ankink
ANKINK_MESQUITE_CURRENT=/var/local/mesquite/ankink-current

# Select the userspace ABI before executing any target code. Both firmware
# families report an ARM CPU, so uname cannot distinguish soft- from hard-float
# userspace. The internal test override is honored only in launcher test mode.
if [ "${ANKINK_LAUNCHER_TEST:-0}" = 1 ]; then
  case "${ANKINK_LAUNCHER_TEST_ABI:-}" in
    armel|armhf) ANKINK_ABI=$ANKINK_LAUNCHER_TEST_ABI ;;
    *) echo "ANKINK_LAUNCHER_TEST_ABI must be armel or armhf" >&2; exit 2 ;;
  esac
elif [ -e /lib/ld-linux-armhf.so.3 ]; then
  ANKINK_ABI=armhf
elif [ -e /lib/ld-linux.so.3 ]; then
  ANKINK_ABI=armel
else
  echo "Cannot determine a compatible Kindle userspace ABI" >> "$ANKINK_LOG"
  exit 1
fi

ANKINK_RUNTIME="$ANKINK_ROOT/$ANKINK_ABI"
case "$ANKINK_ABI" in
  armhf) ANKINK_LOADER="$ANKINK_RUNTIME/lib/ld-linux-armhf.so.3" ;;
  armel) ANKINK_LOADER="$ANKINK_RUNTIME/lib/ld-linux.so.3" ;;
esac
ANKINK_LIBRARY_PATH="$ANKINK_RUNTIME/lib"
ANKINK_DAEMON="$ANKINK_RUNTIME/bin/ankinkd"
ANKINK_PRELOAD="$ANKINK_RUNTIME/lib/libmesquite-whisper-touch.so"
ANKINK_USE_WHISPER_TOUCH=0
if [ "$ANKINK_ABI" = armel ]; then
  ANKINK_USE_WHISPER_TOUCH=1
elif [ -r /etc/version.txt ] && grep -Eqi '(zelda|stinger)' /etc/version.txt; then
  # Oasis 2/3 use ARMHF on current firmware and retain physical page buttons.
  # KOA3 was verified with this helper on firmware 5.18.2.1.1.
  ANKINK_USE_WHISPER_TOUCH=1
fi

if [ ! -x "$ANKINK_DAEMON" ] || [ ! -x "$ANKINK_LOADER" ] ||
   { [ "$ANKINK_USE_WHISPER_TOUCH" = 1 ] && [ ! -f "$ANKINK_PRELOAD" ]; }; then
  echo "Incomplete bundled $ANKINK_ABI runtime" >> "$ANKINK_LOG"
  exit 1
fi

if [ "${ANKINK_LAUNCHER_TEST:-0}" = 1 ]; then
  echo "ABI=$ANKINK_ABI"
  echo "RUNTIME=$ANKINK_RUNTIME"
  echo "EXECUTABLE=$ANKINK_DAEMON"
  echo "LOADER=$ANKINK_LOADER"
  echo "LIBRARY_PATH=$ANKINK_LIBRARY_PATH"
  if [ "$ANKINK_USE_WHISPER_TOUCH" = 1 ]; then
    echo "WHISPER_TOUCH_PRELOAD=enabled"
    echo "PRELOAD=$ANKINK_PRELOAD"
  else
    echo "WHISPER_TOUCH_PRELOAD=disabled"
    echo "PRELOAD="
  fi
  echo "ASSETS=$ANKINK_ROOT/share/ankink"
  exit 0
fi

# KUAL may terminate the action's process group. Registration and daemon
# startup therefore run in a detached session.
if [ "${ANKINK_LAUNCHER:-0}" != 1 ]; then
  ANKINK_LAUNCHER=1 setsid "$0" "$@" </dev/null >/dev/null 2>&1 &
  exit 0
fi

# appmgrd's start operation may only foreground a paused Mesquite process. Stop
# the registered application first, then terminate a stale process if this
# firmware did not unload it promptly.
lipc-set-prop com.lab126.appmgrd stop "app://$ANKINK_APP_ID" >/dev/null 2>&1 || true
sleep 1
for ANKINK_PROC in /proc/[0-9]*; do
  ANKINK_PROC_PID=${ANKINK_PROC##*/}
  ANKINK_PROC_CMD=$(tr '\000' ' ' < "$ANKINK_PROC/cmdline" 2>/dev/null || true)
  case "$ANKINK_PROC_CMD" in
    *"/usr/bin/mesquite -l $ANKINK_APP_ID "*) kill "$ANKINK_PROC_PID" 2>/dev/null || true ;;
  esac
done

# Every KUAL launch is a clean backend launch. Validate the pidfile before
# signaling it, and also catch an orphan left by an interrupted older launcher.
if [ -f "$ANKINK_PID_FILE" ]; then
  read -r ANKINK_DAEMON_PID < "$ANKINK_PID_FILE" || ANKINK_DAEMON_PID=
  if [ -n "$ANKINK_DAEMON_PID" ] && kill -0 "$ANKINK_DAEMON_PID" 2>/dev/null &&
     tr '\000' ' ' < "/proc/$ANKINK_DAEMON_PID/cmdline" 2>/dev/null | grep -q 'ankinkd'; then
    kill "$ANKINK_DAEMON_PID" 2>/dev/null || true
  fi
fi
rm -f "$ANKINK_PID_FILE"
for ANKINK_PROC in /proc/[0-9]*; do
  ANKINK_PROC_PID=${ANKINK_PROC##*/}
  ANKINK_PROC_CMD=$(tr '\000' ' ' < "$ANKINK_PROC/cmdline" 2>/dev/null || true)
  case "$ANKINK_PROC_CMD" in
    *"/mnt/us/ankink/bin/ankinkd "*|*"/mnt/us/ankink/armel/bin/ankinkd "*|*"/mnt/us/ankink/armhf/bin/ankinkd "*)
      kill "$ANKINK_PROC_PID" 2>/dev/null || true
      ;;
  esac
done
sleep 1

mkdir -p "$ANKINK_DATA_DIR"
chmod 700 "$ANKINK_DATA_DIR"
: > "$ANKINK_LOG"
setsid "$ANKINK_LOADER" --library-path "$ANKINK_LIBRARY_PATH" \
  "$ANKINK_DAEMON" \
  --collection "$ANKINK_DATA_DIR/collection.anki2" \
  --assets "$ANKINK_ROOT/share/ankink" \
  --data-dir "$ANKINK_DATA_DIR" \
  --ca-bundle "$ANKINK_ROOT/etc/ca-certificates.crt" \
  --port 9257 >> "$ANKINK_LOG" 2>&1 </dev/null &
ANKINK_DAEMON_PID=$!
echo "$ANKINK_DAEMON_PID" > "$ANKINK_PID_FILE"
sleep 1
if ! kill -0 "$ANKINK_DAEMON_PID" 2>/dev/null; then
  echo "AnkINK daemon failed to start" >> "$ANKINK_LOG"
  rm -f "$ANKINK_PID_FILE"
  exit 1
fi

# A content-specific file URL prevents Mesquite/WebKit's process-global cache
# from serving assets from an older installation.
ANKINK_ASSET_ID=$(cksum "$ANKINK_ROOT/share/ankink/index.html" \
  "$ANKINK_ROOT/share/ankink/app.css" "$ANKINK_ROOT/share/ankink/app-settings.js" | \
  cksum | awk '{print $1}')
ANKINK_MESQUITE_DIR="/var/local/mesquite/ankink-$ANKINK_ASSET_ID"
ANKINK_STAGE="$ANKINK_MESQUITE_DIR.new"
rm -rf "$ANKINK_STAGE"
mkdir -p "$ANKINK_STAGE"
cp -R "$ANKINK_ROOT/share/ankink/." "$ANKINK_STAGE/"
rm -rf "$ANKINK_MESQUITE_DIR"
mv "$ANKINK_STAGE" "$ANKINK_MESQUITE_DIR"
if [ -f "$ANKINK_MESQUITE_CURRENT" ]; then
  read -r ANKINK_OLD_MESQUITE < "$ANKINK_MESQUITE_CURRENT" || ANKINK_OLD_MESQUITE=
  case "$ANKINK_OLD_MESQUITE" in
    /var/local/mesquite/ankink-[0-9]*)
      if [ "$ANKINK_OLD_MESQUITE" != "$ANKINK_MESQUITE_DIR" ]; then
        rm -rf "$ANKINK_OLD_MESQUITE"
      fi
      ;;
  esac
fi
echo "$ANKINK_MESQUITE_DIR" > "$ANKINK_MESQUITE_CURRENT"
rm -rf /var/local/mesquite/ankink

ANKINK_MESQUITE_COMMAND="/usr/bin/mesquite -l $ANKINK_APP_ID -c file://$ANKINK_MESQUITE_DIR/"
if [ "$ANKINK_USE_WHISPER_TOUCH" = 1 ]; then
  ANKINK_MESQUITE_COMMAND="/usr/bin/env LD_PRELOAD=$ANKINK_PRELOAD $ANKINK_MESQUITE_COMMAND"
fi

sqlite3 "$ANKINK_APPREG" <<EOF
BEGIN IMMEDIATE;
INSERT OR IGNORE INTO interfaces(interface) VALUES('application');
INSERT OR IGNORE INTO handlerIds(handlerId) VALUES('$ANKINK_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value)
  VALUES('$ANKINK_APP_ID','lipcId','$ANKINK_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value)
  VALUES('$ANKINK_APP_ID','command','$ANKINK_MESQUITE_COMMAND');
INSERT OR REPLACE INTO properties(handlerId,name,value)
  VALUES('$ANKINK_APP_ID','supportedOrientation','UDLR');
INSERT OR REPLACE INTO properties(handlerId,name,value)
  VALUES('$ANKINK_APP_ID','unloadPolicy','unloadOnPause');
COMMIT;
EOF

lipc-set-prop com.lab126.appmgrd start "app://$ANKINK_APP_ID" >> "$ANKINK_LOG" 2>&1 &
