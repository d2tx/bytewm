#!/bin/sh
# bytewm settings helper: set resolution on the connected output
OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}')
[ -z "$OUT" ] && exit 1
xrandr --output "$OUT" --mode "$1" 2>/dev/null
