#!/bin/sh
# bytewm settings helper: set resolution (+ optional refresh rate) and remember
# it PER OUTPUT (e.g. "eDP-1 1920x1080 60"). This stops the saved external
# resolution from being force-applied to the laptop panel on reconnect/restart.
# prefer the primary output (matches bytewm-disp); the laptop panel stays
# listed as "connected" when --off, so first-connected is wrong
OUT=$(xrandr --current 2>/dev/null | awk '/ primary/{print $1; exit}')
[ -z "$OUT" ] && OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}')
[ -z "$OUT" ] && exit 1
if [ -n "$2" ]; then
	xrandr --output "$OUT" --mode "$1" --rate "$2" 2>/dev/null
else
	xrandr --output "$OUT" --mode "$1" 2>/dev/null
fi
printf '%s %s %s\n' "$OUT" "$1" "${2:-}" > "$HOME/.config/bytewm/resolution"
