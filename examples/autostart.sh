#!/bin/sh
# bytewm autostart

# wallpaper
[ -f ~/.config/bytewm/wallpaper.jpg ] && feh --bg-scale ~/.config/bytewm/wallpaper.jpg &

# notification daemon
bytify < /dev/null &
