#!/bin/sh
# bytewm autostart

# wallpaper
[ -f ~/.config/bytewm/wallpaper.jpg ] && feh --bg-scale ~/.config/bytewm/wallpaper.jpg &

# notification daemon
rm -f /tmp/bytify.fifo
mkfifo /tmp/bytify.fifo 2>/dev/null
bytify &
