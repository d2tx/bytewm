#!/bin/sh
echo "==> bytewm apps setup for Arch Linux"
echo ""

BASE="https://raw.githubusercontent.com/d2tx/bytewm/master/examples"
install() { sudo pacman -S --needed --noconfirm "$@" || true; }

read -p "Install Qt5/6 theming (qt5ct + qt6ct + gruvbox)? [y/N] " qt
read -p "Install OpenCode (coding AI agent)?        [y/N] " oc
read -p "Install media player (mpv)?              [y/N] " mp
read -p "Install music player (ncmpcpp + mpd)?     [y/N] " mc
read -p "Install Firefox?                          [y/N] " ff
read -p "Install qutebrowser (vim-like browser)?    [y/N] " qb
read -p "Install file manager (thunar)?            [y/N] " th
read -p "Install compositor (picom)?               [y/N] " pc
read -p "Install PDF viewer (zathura)?             [y/N] " za
read -p "Install archive tools (unzip, unrar, p7zip)? [y/N] " ar
read -p "Install torrent client (rtorrent)?         [y/N] " rt
echo ""

# ── Install selected ────────────────────────────────────
[ "$qt" = "y" ] || [ "$qt" = "Y" ] && install qt5ct qt6ct
[ "$oc" = "y" ] || [ "$oc" = "Y" ] && install opencode
[ "$mp" = "y" ] || [ "$mp" = "Y" ] && install mpv
[ "$mc" = "y" ] || [ "$mc" = "Y" ] && install ncmpcpp mpd
[ "$ff" = "y" ] || [ "$ff" = "Y" ] && install firefox
[ "$qb" = "y" ] || [ "$qb" = "Y" ] && install qutebrowser
[ "$th" = "y" ] || [ "$th" = "Y" ] && install thunar
[ "$pc" = "y" ] || [ "$pc" = "Y" ] && install picom
[ "$za" = "y" ] || [ "$za" = "Y" ] && install zathura zathura-pdf-mupdf
[ "$ar" = "y" ] || [ "$ar" = "Y" ] && install unzip unrar p7zip
[ "$rt" = "y" ] || [ "$rt" = "Y" ] && install rtorrent

# ── qt5ct / qt6ct ────────────────────────────────────────
if [ "$qt" = "y" ] || [ "$qt" = "Y" ]; then
	echo "==> Setting up Qt theming..."
	mkdir -p "$HOME/.config/qt5ct/colors" "$HOME/.config/qt6ct/colors"

	curl -fsSL "$BASE/qt5ct-gruvbox.conf" -o "$HOME/.config/qt5ct/colors/gruvbox.conf"
	curl -fsSL "$BASE/qt6ct-gruvbox.conf" -o "$HOME/.config/qt6ct/colors/gruvbox.conf"
	curl -fsSL "$BASE/qt5ct.conf" | sed "s|%HOME|$HOME|" > "$HOME/.config/qt5ct/qt5ct.conf"
	curl -fsSL "$BASE/qt6ct.conf" | sed "s|%HOME|$HOME|" > "$HOME/.config/qt6ct/qt6ct.conf"

	# env var: bash
	if ! grep -q 'QT_QPA_PLATFORMTHEME' "$HOME/.bash_profile" 2>/dev/null; then
		echo 'export QT_QPA_PLATFORMTHEME=qt5ct' >> "$HOME/.bash_profile"
	fi
	# env var: fish
	if [ -f "$HOME/.config/fish/config.fish" ]; then
		if ! grep -q 'QT_QPA_PLATFORMTHEME' "$HOME/.config/fish/config.fish" 2>/dev/null; then
			echo 'set -gx QT_QPA_PLATFORMTHEME qt5ct' >> "$HOME/.config/fish/config.fish"
		fi
	fi
	echo "     qt5ct + qt6ct configured (Fusion + gruvbox, Terminus font)"
fi

# ── mpd ──────────────────────────────────────────────────
if [ "$mc" = "y" ] || [ "$mc" = "Y" ]; then
	echo "==> Configuring mpd..."
	mkdir -p "$HOME/.config/mpd/playlists" "$HOME/music"
	curl -fsSL "$BASE/mpd.conf" -o "$HOME/.config/mpd/mpd.conf"
	# starts on login via bytewm autostart (no systemd --user session required)
	pkill mpd 2>/dev/null
	mpd "$HOME/.config/mpd/mpd.conf" 2>/dev/null || true
	echo "     mpd configured (starts via autostart)"

	echo "==> Configuring ncmpcpp..."
	mkdir -p "$HOME/.config/ncmpcpp"
	curl -fsSL "$BASE/ncmpcpp.conf" -o "$HOME/.config/ncmpcpp/config"
	echo "     ncmpcpp configured (bitrate shown for current song)"
fi

echo ""
echo "============================================"
echo "  Apps setup complete!"
echo ""
[ "$qt" = "y" ] || [ "$qt" = "Y" ] && echo "  qt5ct+qt6ct    Fusion + gruvbox (qt5ct active; switch to qt6ct later if needed)"
[ "$oc" = "y" ] || [ "$oc" = "Y" ] && echo "  opencode       coding AI agent"
[ "$mp" = "y" ] || [ "$mp" = "Y" ] && echo "  mpv            media player"
[ "$mc" = "y" ] || [ "$mc" = "Y" ] && echo "  ncmpcpp + mpd  music player (ncurses)"
[ "$ff" = "y" ] || [ "$ff" = "Y" ] && echo "  firefox        web browser"
[ "$qb" = "y" ] || [ "$qb" = "Y" ] && echo "  qutebrowser    vim-like web browser"
[ "$th" = "y" ] || [ "$th" = "Y" ] && echo "  thunar         file manager"
[ "$pc" = "y" ] || [ "$pc" = "Y" ] && echo "  picom          compositor (transparency / shadows)"
[ "$za" = "y" ] || [ "$za" = "Y" ] && echo "  zathura        PDF viewer (vim keys)"
[ "$ar" = "y" ] || [ "$ar" = "Y" ] && echo "  archives       unzip, unrar, p7zip"
[ "$rt" = "y" ] || [ "$rt" = "Y" ] && echo "  rtorrent       torrent client (ncurses)"
echo "============================================"
