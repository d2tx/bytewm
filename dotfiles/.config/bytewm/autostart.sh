#!/bin/sh
# bytewm autostart

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
