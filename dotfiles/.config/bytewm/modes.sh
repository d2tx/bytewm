#!/bin/sh
# bytewm settings helper: list available resolutions on the connected output
OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}')
[ -z "$OUT" ] && exit 0
xrandr --current 2>/dev/null | awk -v home="$HOME" '
	/ connected/ {next}
	/Screen 0/ {next}
	$1 ~ /^[0-9]+x[0-9]+$/ { print $1 " | \"" home "/.config/bytewm/res.sh\" " $1 }
'
