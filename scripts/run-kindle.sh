#!/bin/sh
set -eu

ANKINK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ANKINK_APP_ID=org.ankink.app
ANKINK_MESQUITE_DIR=/var/local/mesquite/ankink
ANKINK_APPREG=/var/local/appreg.db
ANKINK_PID_FILE=/var/tmp/ankinkd.pid
ANKINK_LOG="$ANKINK_ROOT/ankinkd.log"
ANKINK_DATA_DIR=/var/local/ankink

# KUAL may terminate the action's process group. Registration and daemon
# startup therefore run in a detached session.
if [ "${ANKINK_LAUNCHER:-0}" != 1 ]; then
  ANKINK_LAUNCHER=1 setsid "$0" "$@" </dev/null >/dev/null 2>&1 &
  exit 0
fi

ANKINK_DAEMON_RUNNING=0
if [ -f "$ANKINK_PID_FILE" ]; then
  read -r ANKINK_DAEMON_PID < "$ANKINK_PID_FILE" || ANKINK_DAEMON_PID=
  if [ -n "$ANKINK_DAEMON_PID" ] && kill -0 "$ANKINK_DAEMON_PID" 2>/dev/null &&
     tr '\000' ' ' < "/proc/$ANKINK_DAEMON_PID/cmdline" 2>/dev/null | grep -q 'ankinkd'; then
    ANKINK_DAEMON_RUNNING=1
  else
    rm -f "$ANKINK_PID_FILE"
  fi
fi

if [ "$ANKINK_DAEMON_RUNNING" -eq 0 ]; then
  mkdir -p "$ANKINK_DATA_DIR"
  chmod 700 "$ANKINK_DATA_DIR"
  if [ -x "$ANKINK_ROOT/lib/ld-linux-armhf.so.3" ]; then
    ANKINK_LOADER="$ANKINK_ROOT/lib/ld-linux-armhf.so.3"
  elif [ -x "$ANKINK_ROOT/lib/ld-linux.so.3" ]; then
    ANKINK_LOADER="$ANKINK_ROOT/lib/ld-linux.so.3"
  else
    echo "Missing bundled ARM dynamic loader" >> "$ANKINK_LOG"
    exit 1
  fi
  : > "$ANKINK_LOG"
  setsid "$ANKINK_LOADER" --library-path "$ANKINK_ROOT/lib" \
    "$ANKINK_ROOT/bin/ankinkd" \
    --collection "$ANKINK_DATA_DIR/collection.anki2" \
    --assets "$ANKINK_ROOT/share/ankink" \
    --port 8765 >> "$ANKINK_LOG" 2>&1 </dev/null &
  ANKINK_DAEMON_PID=$!
  echo "$ANKINK_DAEMON_PID" > "$ANKINK_PID_FILE"
  sleep 1
  if ! kill -0 "$ANKINK_DAEMON_PID" 2>/dev/null; then
    echo "AnkINK daemon failed to start" >> "$ANKINK_LOG"
    rm -f "$ANKINK_PID_FILE"
    exit 1
  fi
fi

ANKINK_STAGE=/var/local/mesquite/ankink.new
rm -rf "$ANKINK_STAGE"
mkdir -p "$ANKINK_STAGE"
cp -R "$ANKINK_ROOT/share/ankink/." "$ANKINK_STAGE/"
rm -rf "$ANKINK_MESQUITE_DIR"
mv "$ANKINK_STAGE" "$ANKINK_MESQUITE_DIR"

sqlite3 "$ANKINK_APPREG" <<EOF
BEGIN IMMEDIATE;
INSERT OR IGNORE INTO interfaces(interface) VALUES('application');
INSERT OR IGNORE INTO handlerIds(handlerId) VALUES('$ANKINK_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value)
  VALUES('$ANKINK_APP_ID','lipcId','$ANKINK_APP_ID');
INSERT OR REPLACE INTO properties(handlerId,name,value)
  VALUES('$ANKINK_APP_ID','command','/usr/bin/mesquite -l $ANKINK_APP_ID -c file://$ANKINK_MESQUITE_DIR/');
INSERT OR REPLACE INTO properties(handlerId,name,value)
  VALUES('$ANKINK_APP_ID','supportedOrientation','U');
COMMIT;
EOF

lipc-set-prop com.lab126.appmgrd start "app://$ANKINK_APP_ID" >> "$ANKINK_LOG" 2>&1 &
