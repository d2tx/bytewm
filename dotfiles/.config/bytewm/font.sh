#!/bin/sh
# bytewm settings helper: set global font and propagate everywhere
FONT="$1"
[ -z "$FONT" ] && exit 1
CFG="$HOME/.config/bytewm/config"

# 1. bytewm bar font + shared app font file
if [ -f "$CFG" ]; then
	if grep -q '^font =' "$CFG"; then
		sed -i "s|^font = .*|font = $FONT|" "$CFG"
	else
		echo "font = $FONT" >> "$CFG"
	fi
fi
printf '%s\n' "$FONT" > "$HOME/.config/bytewm/font"

# 2. st Xresources (st.font) - applies to new terminals
XR="$HOME/.Xresources"
[ -f "$XR" ] || touch "$XR"
if grep -q '^st\.font:' "$XR"; then
	sed -i "s|^st\.font:.*|st.font: $FONT|" "$XR"
else
	printf 'st.font: %s\n' "$FONT" >> "$XR"
fi
xrdb -merge "$XR" 2>/dev/null

# 3. GTK3 settings.ini (gtk-font-name "Family Size") - only when a size is given
GTK="$HOME/.config/gtk-3.0/settings.ini"
FAMILY=$(echo "$FONT" | sed 's/:size=.*//; s/:.*//')
SIZE=$(echo "$FONT" | sed -n 's/.*:size=\([0-9]*\).*/\1/p')
if [ -n "$SIZE" ] && [ -f "$GTK" ]; then
	if grep -q '^gtk-font-name=' "$GTK"; then
		sed -i "s|^gtk-font-name=.*|gtk-font-name=$FAMILY $SIZE|" "$GTK"
	else
		printf 'gtk-font-name=%s %s\n' "$FAMILY" "$SIZE" >> "$GTK"
	fi
fi

# 4. reload bytewm (re-reads config font)
pkill -HUP bytewm 2>/dev/null
