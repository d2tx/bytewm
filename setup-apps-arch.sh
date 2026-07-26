#!/bin/sh
echo "==> bytewm apps setup for Arch Linux"
echo ""

install() { sudo pacman -S --needed --noconfirm "$@" || true; }

read -p "Install Qt5/6 theming (qt5ct + qt6ct + gruvbox)? [y/N] " qt
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
[ "$qt" = "y" ] || [ "$qt" = "Y" ] && install qt5ct qt6ct
[ "$mp" = "y" ] || [ "$mp" = "Y" ] && install mpv
[ "$mc" = "y" ] || [ "$mc" = "Y" ] && install ncmpcpp mpd
[ "$ff" = "y" ] || [ "$ff" = "Y" ] && install firefox
[ "$th" = "y" ] || [ "$th" = "Y" ] && install thunar
[ "$pc" = "y" ] || [ "$pc" = "Y" ] && install picom
[ "$za" = "y" ] || [ "$za" = "Y" ] && install zathura zathura-pdf-mupdf
[ "$pv" = "y" ] || [ "$pv" = "Y" ] && install pavucontrol
[ "$ar" = "y" ] || [ "$ar" = "Y" ] && install unzip unrar p7zip
[ "$rt" = "y" ] || [ "$rt" = "Y" ] && install rtorrent

# ── qt5ct setup ──────────────────────────────────────────
if [ "$qt" = "y" ] || [ "$qt" = "Y" ]; then
	mkdir -p "$HOME/.config/qt5ct/colors" "$HOME/.config/qt6ct/colors"

	# gruvbox color scheme (fusion style palette) — shared by qt5ct and qt6ct
	if [ ! -f "$HOME/.config/qt5ct/colors/gruvbox.conf" ]; then
		cat > "$HOME/.config/qt5ct/colors/gruvbox.conf" << 'QEOF'
[ColorScheme]
active_colors=#ebdbb2, #3c3836, #504945, #3c3836, #1d2021, #504945, #ebdbb2, #ebdbb2, #ebdbb2, #282828, #282828, #1d2021, #689d6a, #282828, #458588, #b16286, #3c3836, #000000, #3c3836, #ebdbb2
inactive_colors=#a89984, #3c3836, #504945, #3c3836, #282828, #504945, #a89984, #a89984, #a89984, #282828, #282828, #282828, #504945, #a89984, #458588, #b16286, #3c3836, #000000, #3c3836, #a89984
disabled_colors=#928374, #3c3836, #504945, #3c3836, #282828, #504945, #928374, #928374, #928374, #282828, #282828, #282828, #3c3836, #928374, #458588, #b16286, #3c3836, #000000, #3c3836, #928374
QEOF
	fi
	cp "$HOME/.config/qt5ct/colors/gruvbox.conf" "$HOME/.config/qt6ct/colors/gruvbox.conf" 2>/dev/null || true

	# qt5ct.conf
	cat > "$HOME/.config/qt5ct/qt5ct.conf" << 'QEOF'
[Appearance]
custom_palette=true
standard_dialogs=default
style=Fusion
color_scheme_path=%HOME/.config/qt5ct/colors/gruvbox.conf

[Fonts]
fixed="xos4 Terminus,9,-1,5,50,0,0,0,0,0,Regular"
general="xos4 Terminus,9,-1,5,50,0,0,0,0,0,Regular"

[Interface]
activate_item_on_single_click=1
buttonbox_layout=0
cursor_flash_time=1000
dialog_buttons_have_icons=1
double_click_interval=400
gui_effects=@Invalid()
keyboard_scheme=2
menus_have_icons=true
show_shortcuts_in_context_menus=true
stylesheets=@Invalid()
toolbutton_style=4
underline_shortcut=1
wheel_scroll_lines=3

[Troubleshooting]
force_raster_widgets=1
ignored_applications=@Invalid()
QEOF
	sed -i "s|%HOME|$HOME|" "$HOME/.config/qt5ct/qt5ct.conf"

	# qt6ct.conf
	cat > "$HOME/.config/qt6ct/qt6ct.conf" << 'QEOF'
[Appearance]
custom_palette=true
standard_dialogs=default
style=Fusion
color_scheme_path=%HOME/.config/qt6ct/colors/gruvbox.conf

[Fonts]
fixed="xos4 Terminus,9,-1,5,50,0,0,0,0,0,Regular"
general="xos4 Terminus,9,-1,5,50,0,0,0,0,0,Regular"

[Interface]
activate_item_on_single_click=1
buttonbox_layout=0
cursor_flash_time=1000
dialog_buttons_have_icons=1
double_click_interval=400
gui_effects=@Invalid()
keyboard_scheme=2
menus_have_icons=true
show_shortcuts_in_context_menus=true
stylesheets=@Invalid()
toolbutton_style=4
underline_shortcut=1
wheel_scroll_lines=3

[Troubleshooting]
force_raster_widgets=1
ignored_applications=@Invalid()
QEOF
	sed -i "s|%HOME|$HOME|" "$HOME/.config/qt6ct/qt6ct.conf"

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

	echo "     qt5ct + qt6ct installed (qt5ct active: Fusion + gruvbox, Terminus font)"
fi

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
[ "$qt" = "y" ] || [ "$qt" = "Y" ] && echo "  qt5ct+qt6ct    Fusion + gruvbox (qt5ct active; switch to qt6ct later if needed)"
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
