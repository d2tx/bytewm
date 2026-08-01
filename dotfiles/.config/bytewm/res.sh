#!/bin/sh
# bytewm settings helper: set resolution (+ optional refresh rate) and remember it
OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}')
[ -z "$OUT" ] && exit 1
if [ -n "$2" ]; then
	xrandr --output "$OUT" --mode "$1" --rate "$2" 2>/dev/null
else
	xrandr --output "$OUT" --mode "$1" 2>/dev/null
fi
printf '%s %s\n' "$1" "${2:-}" > "$HOME/.config/bytewm/resolution"
