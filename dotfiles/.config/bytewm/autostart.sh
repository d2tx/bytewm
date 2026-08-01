#!/bin/sh
# bytewm autostart

# reapply saved resolution + refresh rate
if [ -f ~/.config/bytewm/resolution ]; then
	read resmode resrate < ~/.config/bytewm/resolution
	[ -n "$resmode" ] && [ -n "$resrate" ] && \
		OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}') && \
		[ -n "$OUT" ] && xrandr --output "$OUT" --mode "$resmode" --rate "$resrate" 2>/dev/null
fi

# wallpaper (checks .png first, then .jpg)
if [ -f ~/.config/bytewm/wallpaper.png ]; then
	feh --bg-fill ~/.config/bytewm/wallpaper.png &
elif [ -f ~/.config/bytewm/wallpaper.jpg ]; then
	feh --bg-fill ~/.config/bytewm/wallpaper.jpg &
fi

# notification daemon
pkill bytify 2>/dev/null
rm -f /tmp/bytify.fifo
mkfifo /tmp/bytify.fifo
bytify &

# volume OSD daemon
pkill bytevol 2>/dev/null
rm -f /tmp/bytevol.fifo
mkfifo /tmp/bytevol.fifo
bytevol &
