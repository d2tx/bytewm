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
			case "$fam" in
				Cozette)
					if fc-list --format='%{family} %{style}\n' 2>/dev/null | grep -qx 'Cozette Medium'; then
						printf 'Cozette (4x10 small) | __GEN__ "%s/.config/bytewm/fontmenu.sh" sizes "Cozette small"\n' "$HOME"
					fi
					if fc-list --format='%{family} %{style}\n' 2>/dev/null | grep -qx 'Cozette HiDpi'; then
						printf 'Cozette HiDpi (8x20 big) | __GEN__ "%s/.config/bytewm/fontmenu.sh" sizes "Cozette HiDpi"\n' "$HOME"
					fi
					continue
					;;
			esac
			printf '%s | __GEN__ "%s/.config/bytewm/fontmenu.sh" sizes "%s"\n' "$fam" "$HOME" "$fam"
		done
		;;
	sizes)
		fam="$2"
		# does this family have a Bold style installed?
		if fc-list --format='%{family} %{style}\n' 2>/dev/null | grep -qi "^$fam Bold$"; then
			HASBOLD=1
		else
			HASBOLD=0
		fi
		case "$fam" in
			Terminus|"xos4 Terminus")
				for sz in 12 14 16 18 20 22 24 28 32; do
					printf '%s | "%s/.config/bytewm/font.sh" "%s:size=%s"\n' "$sz" "$HOME" "$fam" "$sz"
					[ "$HASBOLD" = 1 ] && \
						printf '%s bold | "%s/.config/bytewm/font.sh" "%s:size=%s:bold"\n' "$sz" "$HOME" "$fam" "$sz"
				done
				exit 0
				;;
		esac
		case "$fam" in
			"Cozette small")
				for sz in 10 13 16; do
					printf '%s | "%s/.config/bytewm/font.sh" "Cozette:size=%s"\n' "$sz" "$HOME" "$sz"
				done
				exit 0
				;;
			"Cozette HiDpi")
				for sz in 20 26 32; do
					printf '%s | "%s/.config/bytewm/font.sh" "Cozette:size=%s"\n' "$sz" "$HOME" "$sz"
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
		for sz in 8 9 10 11 12 13 14 16 18 20 22 24 26 32; do
			printf '%s | "%s/.config/bytewm/font.sh" "%s:size=%s"\n' "$sz" "$HOME" "$fam" "$sz"
			[ "$HASBOLD" = 1 ] && \
				printf '%s bold | "%s/.config/bytewm/font.sh" "%s:size=%s:bold"\n' "$sz" "$HOME" "$fam" "$sz"
		done
		;;
esac
