#!/bin/sh
# bytewm autostart

# wallpaper
[ -f ~/bytewm/wallpaper.jpg ] && feh --bg-scale ~/bytewm/wallpaper.jpg &

# notification daemon
rm -f /tmp/bytify.fifo
mkfifo /tmp/bytify.fifo 2>/dev/null
bytify &
