#!/bin/sh
# bytewm settings helper: list font families or sizes for a family
BITMAPS="fixed 6x13 9x15 10x20 Terminus"
case "$1" in
	families)
		for b in $BITMAPS; do
			printf '%s | __GEN__ "%s/.config/bytewm/fontmenu.sh" sizes "%s"\n' "$b" "$HOME" "$b"
		done
		fc-list --format='%{family}\n' 2>/dev/null | sed 's/,.*//' | sort -u | while read -r fam; do
			[ -z "$fam" ] && continue
			case "$fam" in *.pcf) continue ;; esac
			case " $BITMAPS " in *" $fam "*) continue ;; esac
			printf '%s | __GEN__ "%s/.config/bytewm/fontmenu.sh" sizes "%s"\n' "$fam" "$HOME" "$fam"
		done
		;;
	sizes)
		fam="$2"
		case "$fam" in
			Terminus|"xos4 Terminus")
				for sz in 12 14 16 18 20 22 24 28 32; do
					printf '%s | "%s/.config/bytewm/font.sh" "%s:size=%s"\n' "$sz" "$HOME" "$fam" "$sz"
				done
				exit 0
				;;
		esac
		case " $BITMAPS " in
			*" $fam "*)
				printf 'default | "%s/.config/bytewm/font.sh" "%s"\n' "$HOME" "$fam"
				exit 0
				;;
		esac
		for sz in 8 9 10 11 12 13 14 16; do
			printf '%s | "%s/.config/bytewm/font.sh" "%s:size=%s"\n' "$sz" "$HOME" "$fam" "$sz"
		done
		;;
esac
