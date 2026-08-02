#!/bin/sh
set -e

# ensure curl is available for bootstrap
if ! command -v curl >/dev/null 2>&1; then
  if command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --noconfirm curl
  fi
fi

# if being piped via curl (not inside a repo checkout), fetch source first
if [ ! -f "$(dirname "$0")/bytewm.c" ]; then
  echo "==> Downloading bytewm..."
  curl -fsSL https://github.com/d2tx/bytewm/archive/master.tar.gz | tar xz
  cd bytewm-master && sh setup-arch.sh && exit
fi

echo "==> bytewm setup for Arch Linux"
echo "==> Installing packages..."
sudo pacman -S --needed --noconfirm \
  base-devel libx11 libxft fontconfig freetype2 xorg-server xorg-xinit alsa-utils alsa-lib alsa-plugins \
  pipewire pipewire-pulse pipewire-alsa wireplumber \
  feh xorg-xrdb xorg-xrandr xorg-fonts-misc pam git ttf-dejavu terminus-font \
  smartmontools bat fish neovim ranger

echo "==> Rebuilding font cache (ensures bitmap/Xft fonts are registered)..."
fc-cache -f 2>/dev/null || true

echo "==> Installing Cozette bitmap font (GitHub release, not AUR)..."
mkdir -p "$HOME/.local/share/fonts"
COZETTE_VER="v.1.30.0"
for f in cozette.otb cozette_hidpi.otb; do
  if [ ! -f "$HOME/.local/share/fonts/$f" ]; then
    curl -fsSL -o "$HOME/.local/share/fonts/$f" \
      "https://github.com/the-moonwitch/Cozette/releases/download/$COZETTE_VER/$f" \
      || echo "     WARNING: failed to fetch $f"
  fi
done
fc-cache -f 2>/dev/null || true
if fc-list 2>/dev/null | grep -qi '^Cozette'; then
  echo "     Cozette installed"
else
  echo "     WARNING: Cozette not registered"
fi


echo "==> Adding user to audio group (for ALSA access)..."
sudo usermod -aG audio "$USER"
echo "     done (re-login needed)"

echo "==> Passwordless systemctl power actions (for bytewm-exit)..."
if [ ! -f /etc/sudoers.d/bytewm ]; then
	echo "%wheel ALL=(ALL) NOPASSWD: /usr/bin/systemctl reboot, /usr/bin/systemctl poweroff, /usr/bin/systemctl suspend" | sudo tee /etc/sudoers.d/bytewm >/dev/null
	sudo chmod 440 /etc/sudoers.d/bytewm
fi
echo "     done"

echo "==> Audio setup (PipeWire + ALSA)"
echo "     PipeWire provides systemwide audio; pipewire-alsa routes ALSA"
echo "     \"default\" through PipeWire so all apps follow the active sink."
echo "     (No /etc/asound.conf pcm.!default override - that would bypass it.)"
cards=$(aplay -l 2>/dev/null | sed -nE 's/^card ([0-9]+): ([^[]+).*/\1|\2/p' | awk '!seen[$1]++' | sed -E 's/[[:space:]]+$//')
if [ -z "$cards" ]; then
  echo "WARNING: no audio card found — skipping mixer setup"
else
  idx=${cards%%|*}
  name=${cards#*|}
  echo "  Default audio card: $name"
  # leave pcm.!default alone (pipewire-alsa owns it); only ensure mixer state
  sudo tee /etc/asound.conf >/dev/null <<EOF
# mixer control -> the chosen card (playback stays on PipeWire's default)
ctl.!default {
    type hw
    card "$name"
}
EOF
  amixer -c "$idx" set Master 100% unmute >/dev/null 2>&1 || true
  amixer -c "$idx" set PCM 100% unmute >/dev/null 2>&1 || true
  sudo alsactl store 2>/dev/null || true
fi

echo "==> Installing oh-my-fish..."
if ! fish -c 'omf version' 2>/dev/null; then
  git clone --depth 1 https://github.com/oh-my-fish/oh-my-fish "$HOME/.local/share/omf" 2>/dev/null
fi

echo "==> Setting fish as default shell..."
sudo chsh -s /usr/bin/fish "$USER" 2>/dev/null || true

echo "==> Generating ranger config..."
ranger --copy-config=all 2>/dev/null || true

echo "==> Restoring dotfiles..."
cp -a dotfiles/.bashrc dotfiles/.bash_profile dotfiles/.bash_logout "$HOME/" 2>/dev/null || true
cp -a dotfiles/.xinitrc dotfiles/.fehbg "$HOME/" 2>/dev/null || true
cp -a dotfiles/.config/fish "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/ranger "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/opencode "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/gtk-3.0 "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/qutebrowser "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/nvim "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/bytewm "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/bytemenu "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/fontconfig "$HOME/.config/" 2>/dev/null || true
fc-cache -f 2>/dev/null || true
echo "     done"

echo "==> Installing GTK theme..."
mkdir -p "$HOME/.themes"
cp -r themes/bytewm-gruvbox "$HOME/.themes/" 2>/dev/null || true
echo "     done"

echo "==> Installing icon theme..."
mkdir -p "$HOME/.icons"
if [ ! -d "$HOME/.icons/Gruvbox-Plus-Dark" ]; then
  git clone --depth 1 https://github.com/SylEleuth/gruvbox-plus-icon-pack /tmp/gruvbox-icons
  cp -r /tmp/gruvbox-icons/Gruvbox-Plus-Dark "$HOME/.icons/"
  rm -rf /tmp/gruvbox-icons
fi
echo "     done"

echo "==> Generating st Xresources (terminal theme)..."
if [ ! -f "$HOME/.Xresources" ]; then
  cat > "$HOME/.Xresources" << 'XEOF'
st.foreground: #ebdbb2
st.background: #282828
st.cursor:     #689d6a
st.color0:   #1d2021
st.color1:   #cc241d
st.color2:   #98971a
st.color3:   #d79921
st.color4:   #458588
st.color5:   #b16286
st.color6:   #689d6a
st.color7:   #a89984
st.color8:   #928374
st.color9:   #fb4934
st.color10:  #b8bb26
st.color11:  #fabd2f
st.color12:  #83a598
st.color13:  #d3869b
st.color14:  #8ec07c
st.color15:  #ebdbb2
XEOF
fi
xrdb -merge "$HOME/.Xresources" 2>/dev/null || true

echo "==> Building bytewm, apps and bytewdm..."
make clean 2>/dev/null || true
make

echo "==> Installing..."
sudo make install
echo "==> Enabling bytewdm..."
sudo systemctl enable bytewdm 2>/dev/null || true

echo "==> Building st terminal..."
if ! command -v st >/dev/null 2>&1; then
  sudo pacman -S --needed --noconfirm libxft fontconfig
  [ -d ~/st ] || git clone https://github.com/d2tx/st ~/st
  (cd ~/st && make && sudo make install)
else
  echo "     st already installed"
fi

echo ""
echo "  bytewm installed!"
echo ""
echo "  To start:"
echo "    TTY:  startx"
echo "    DM:   sudo /usr/local/bin/bytewdm"
echo ""
  echo "  Keybindings:"
  echo "    Super+Shift+Return  terminal"
  echo "    Super+Return        scratchpad"
  echo "    Super+p              launcher"
  echo "    Super+Shift+m        favorites (bytemenu)"
  echo "    Super+Shift+r        reload config"
  echo "    Super+Shift+q        quit"
  echo "    Super+w              kill window"
  echo "    Super+j/k            focus stack"
  echo "    Super+1-5            tags"
  echo "    Super+b/m/t          layouts (bsp/monocle/tile)"
  echo "    Super+F10/F11/F12    volume"
  echo "    Super+Ctrl+L         lock (bytelock)"
echo ""
echo "============================================"
