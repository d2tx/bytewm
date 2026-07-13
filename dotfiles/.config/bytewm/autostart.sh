#!/bin/sh
# bytewm autostart

# wallpaper
[ -f ~/bytewm/wallpaper.jpg ] && feh --bg-scale ~/bytewm/wallpaper.jpg &

# notification daemon
bytify < /dev/null &
