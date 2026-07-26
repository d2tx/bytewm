#!/bin/sh
echo "==> bytewm apps setup for Arch Linux"
echo ""

install() { sudo pacman -S --needed --noconfirm "$@" || true; }

read -p "Install media player (mpv)?              [y/N] " mp
read -p "Install music player (ncmpcpp + mpd)?     [y/N] " mc
read -p "Install Firefox?                          [y/N] " ff
read -p "Install file manager (thunar)?            [y/N] " th
read -p "Install compositor (picom)?               [y/N] " pc
read -p "Install PDF viewer (zathura)?             [y/N] " za
read -p "Install audio mixer GUI (pavucontrol)?     [y/N] " pv
read -p "Install archive tools (unzip, unrar, p7zip)? [y/N] " ar
read -p "Install torrent client (rtorrent)?         [y/N] " rt
echo ""

# ── Install selected ────────────────────────────────────
[ "$mp" = "y" ] || [ "$mp" = "Y" ] && install mpv
[ "$mc" = "y" ] || [ "$mc" = "Y" ] && install ncmpcpp mpd
[ "$ff" = "y" ] || [ "$ff" = "Y" ] && install firefox
[ "$th" = "y" ] || [ "$th" = "Y" ] && install thunar
[ "$pc" = "y" ] || [ "$pc" = "Y" ] && install picom
[ "$za" = "y" ] || [ "$za" = "Y" ] && install zathura zathura-pdf-mupdf
[ "$pv" = "y" ] || [ "$pv" = "Y" ] && install pavucontrol
[ "$ar" = "y" ] || [ "$ar" = "Y" ] && install unzip unrar p7zip
[ "$rt" = "y" ] || [ "$rt" = "Y" ] && install rtorrent

# ── mpd setup ────────────────────────────────────────────
if [ "$mc" = "y" ] || [ "$mc" = "Y" ]; then
	if ! systemctl --user is-enabled mpd 2>/dev/null | grep -q enabled; then
		mkdir -p "$HOME/.config/mpd/playlists" "$HOME/music"
		if [ ! -f "$HOME/.config/mpd/mpd.conf" ]; then
			cat > "$HOME/.config/mpd/mpd.conf" << 'EOF'
music_directory    "~/music"
playlist_directory "~/.config/mpd/playlists"
db_file            "~/.config/mpd/database"
log_file           "~/.config/mpd/log"
pid_file           "~/.config/mpd/pid"
state_file         "~/.config/mpd/state"
audio_output {
	type  "pulse"
	name  "PulseAudio"
}
EOF
		fi
		systemctl --user enable --now mpd 2>/dev/null || true
		echo "     mpd configured and enabled"
	fi
fi

echo ""
echo "============================================"
echo "  Apps setup complete!"
echo ""
[ "$mp" = "y" ] || [ "$mp" = "Y" ] && echo "  mpv            media player"
[ "$mc" = "y" ] || [ "$mc" = "Y" ] && echo "  ncmpcpp + mpd  music player (ncurses)"
[ "$ff" = "y" ] || [ "$ff" = "Y" ] && echo "  firefox        web browser"
[ "$th" = "y" ] || [ "$th" = "Y" ] && echo "  thunar         file manager"
[ "$pc" = "y" ] || [ "$pc" = "Y" ] && echo "  picom          compositor (transparency / shadows)"
[ "$za" = "y" ] || [ "$za" = "Y" ] && echo "  zathura        PDF viewer (vim keys)"
[ "$pv" = "y" ] || [ "$pv" = "Y" ] && echo "  pavucontrol    audio mixer (per-app volume, device switching)"
[ "$ar" = "y" ] || [ "$ar" = "Y" ] && echo "  archives       unzip, unrar, p7zip"
[ "$rt" = "y" ] || [ "$rt" = "Y" ] && echo "  rtorrent       torrent client (ncurses)"
echo "============================================"
