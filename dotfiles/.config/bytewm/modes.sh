#!/bin/sh
# bytewm settings helper: list resolutions, or refresh rates for one
case "$1" in
	rates)
		xrandr --current 2>/dev/null | awk -v w="$2" -v home="$HOME" '
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
		OUT=$(xrandr --current 2>/dev/null | awk '/ connected/{print $1; exit}')
		[ -z "$OUT" ] && exit 0
		xrandr --current 2>/dev/null | awk -v home="$HOME" '
			/ connected/ {next}
			/Screen 0/ {next}
			$1 ~ /^[0-9]+x[0-9]+$/ { if (!seen[$1]++) print $1 " | __GEN__ \"" home "/.config/bytewm/modes.sh\" rates \"" $1 "\"" }
		'
		;;
esac
