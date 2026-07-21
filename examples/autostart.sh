#!/bin/sh
# bytewm autostart

# wallpaper
# wallpaper (checks .png first, then .jpg)
if [ -f ~/.config/bytewm/wallpaper.png ]; then
	feh --bg-fill ~/.config/bytewm/wallpaper.png &
elif [ -f ~/.config/bytewm/wallpaper.jpg ]; then
	feh --bg-fill ~/.config/bytewm/wallpaper.jpg &
fi

# notification daemon
rm -f /tmp/bytify.fifo
mkfifo /tmp/bytify.fifo 2>/dev/null
bytify &
