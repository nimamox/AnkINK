#!/bin/sh
set -eu
ANKINK_PID_FILE=/var/tmp/ankinkd.pid
if [ -f "$ANKINK_PID_FILE" ]; then
  read -r ANKINK_PID < "$ANKINK_PID_FILE" || ANKINK_PID=
  if [ -n "$ANKINK_PID" ] && kill -0 "$ANKINK_PID" 2>/dev/null &&
     tr '\000' ' ' < "/proc/$ANKINK_PID/cmdline" 2>/dev/null | grep -q 'ankinkd'; then
    kill "$ANKINK_PID"
  fi
  rm -f "$ANKINK_PID_FILE"
fi
