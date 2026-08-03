#!/bin/sh
# bytewm settings helper: list resolutions, or refresh rates for one.
# Only the ACTIVE (primary) output's modes are shown - the display that is
# actually in use (matches bytewm-disp and res.sh). The laptop panel stays
# listed as "connected" even when --off, so "first connected" would be wrong;
# the primary flag is only set on the display that is really driving output.
case "$1" in
	rates)
		OUT=$(xrandr --current 2>/dev/null | awk '/ primary/{print $1; exit}')
		[ -z "$OUT" ] && OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}')
		[ -z "$OUT" ] && exit 0
		xrandr --current 2>/dev/null | awk -v out="$OUT" -v w="$2" -v home="$HOME" '
			/Screen [0-9]+:/ {cur=""; con=0; next}
			/ connected / {cur=$1; con=1; next}
			/ disconnected / {cur=$1; con=0; next}
			cur != out || !con {next}
			$1 == w {
				for (i = 2; i <= NF; i++) {
					r = $i; gsub(/[*+]/, "", r)
					sub(/\.00$/, "", r)
					if (r ~ /^[0-9]+(\.[0-9]+)?$/ && !seen[r]++)
						print r "hz | \"" home "/.config/bytewm/res.sh\" \"" w "\" " r
				}
			}
		'
		;;
	*)
		# prefer the primary output (matches bytewm-disp); the laptop panel
		# stays listed as "connected" when --off, so first-connected is wrong
		OUT=$(xrandr --current 2>/dev/null | awk '/ primary/{print $1; exit}')
		[ -z "$OUT" ] && OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}')
		[ -z "$OUT" ] && exit 0
		xrandr --current 2>/dev/null | awk -v out="$OUT" -v home="$HOME" '
			/Screen [0-9]+:/ {cur=""; con=0; next}
			/ connected / {cur=$1; con=1; next}
			/ disconnected / {cur=$1; con=0; next}
			cur != out || !con {next}
			$1 ~ /^[0-9]+x[0-9]+$/ { if (!seen[$1]++) print $1 " | __GEN__ \"" home "/.config/bytewm/modes.sh\" rates \"" $1 "\"" }
		'
		;;
esac
