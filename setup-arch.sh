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
  cd bytewm-master
  exec sh setup-arch.sh
fi

echo "==> bytewm setup for Arch Linux"
echo "==> Installing packages..."
sudo pacman -S --needed --noconfirm \
  base-devel libx11 xorg-server xorg-xinit alsa-utils \
  feh xorg-xrdb xorg-fonts-misc pam git \
  fish neovim ranger

echo "==> Installing oh-my-fish..."
if ! fish -c 'omf version' 2>/dev/null; then
  curl -fsSL https://raw.githubusercontent.com/oh-my-fish/oh-my-fish/master/bin/install | fish
fi
fish -c 'omf install bobthefish' 2>/dev/null || true

echo "==> Setting fish as default shell..."
chsh -s /usr/bin/fish 2>/dev/null || true

echo "==> Generating ranger config..."
ranger --copy-config=all 2>/dev/null || true

echo "==> Restoring dotfiles..."
cp -a dotfiles/.bashrc dotfiles/.bash_profile dotfiles/.bash_logout "$HOME/" 2>/dev/null || true
cp -a dotfiles/.xinitrc dotfiles/.fehbg "$HOME/" 2>/dev/null || true
cp -a dotfiles/.config/fish "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/ranger "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/opencode "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/gtk-3.0 "$HOME/.config/" 2>/dev/null || true
cp -a dotfiles/.config/bytewm "$HOME/.config/" 2>/dev/null || true
echo "     done"

echo "==> Building bytewm, apps and bytewdm..."
make clean 2>/dev/null || true
make

echo "==> Installing..."
sudo make install
echo "==> Enabling bytewdm..."
sudo systemctl enable bytewdm 2>/dev/null || true

echo "==> Building st terminal..."
if ! command -v st >/dev/null 2>&1; then
  sudo pacman -S --needed --noconfirm libxft
  [ -d ~/st ] || git clone https://git.suckless.org/st ~/st
  cp examples/st-gruvbox.config.def.h ~/st/config.def.h
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
echo "    Super+Shift+q        quit"
echo "    Super+w              kill window"
echo "    Super+j/k            focus stack"
echo "    Super+1-5            tags"
echo "    Super+b/m/t          layouts (bsp/monocle/tile)"
echo ""
echo "============================================"
