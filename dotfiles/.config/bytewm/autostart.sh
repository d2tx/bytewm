#!/bin/sh
# bytewm autostart

# compositor (vsync) - fixes screen tearing
picom &

# load saved Xresources (st.font, st colors) into the X server
xrdb -merge "$HOME/.Xresources" 2>/dev/null

# reapply saved resolution + refresh rate (per-output: "OUTPUT MODE RATE")
if [ -f ~/.config/bytewm/resolution ]; then
	read saved_out saved_mode saved_rate < ~/.config/bytewm/resolution
	[ -n "$saved_out" ] && [ -n "$saved_mode" ] && \
		xrandr --current 2>/dev/null | grep -q "^$saved_out connected" && \
		xrandr --output "$saved_out" --mode "$saved_mode" --rate "$saved_rate" 2>/dev/null
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

# monitor hotplug daemon (external display takes over, like Windows)
pkill bytewm-disp 2>/dev/null
rm -f /tmp/bytewm-disp.fifo
mkfifo /tmp/bytewm-disp.fifo
bytewm-disp &

# music player daemon (for ncmpcpp)
pkill mpd 2>/dev/null
mpd ~/.config/mpd/mpd.conf &

# polkit authentication agent (password dialogs for apps like windscribe)
/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &
