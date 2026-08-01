#!/bin/sh
echo "==> bytewm gaming setup for Arch Linux"
echo ""

if ! grep -q '^\[multilib\]' /etc/pacman.conf; then
  echo "[multilib] not enabled — required for Steam, 32-bit games."
  read -p "Enable multilib and update system now? [y/N] " ml
  if [ "$ml" = "y" ] || [ "$ml" = "Y" ]; then
    echo "==> Enabling multilib + system update..."
    sudo sed -i 's/^#\?\[multilib\]/[multilib]/; /^\[multilib\]/{n;s/^#//}' /etc/pacman.conf
    sudo pacman -Syu --noconfirm
  else
    echo "WARNING: Skipping — Steam and 32-bit packages will fail."
  fi
  echo ""
fi

read -p "Install AMD GPU drivers?        [y/N] " amd
read -p "Install NVIDIA drivers?          [y/N] " nv
read -p "Install 32-bit libraries?        [y/N] " m32
read -p "Install Wine + Winetricks?       [y/N] " wine
read -p "Install umu-launcher (Proton runner)? [y/N] " um
read -p "Install Steam/Lutris/MangoHud?   [y/N] " st
read -p "Install gamepad support?         [y/N] " gp
read -p "Build asusctl from source?       [y/N] " ac
echo ""

install() { sudo pacman -S --needed --noconfirm "$@" || true; }

# ── GPU drivers ──────────────────────────────────────────
if [ "$amd" = "y" ] || [ "$amd" = "Y" ]; then
  echo "==> AMD GPU drivers"
  install mesa vulkan-radeon libva-mesa-driver
fi
if [ "$nv" = "y" ] || [ "$nv" = "Y" ]; then
  echo "==> NVIDIA drivers"
    install nvidia-open-dkms nvidia-utils nvidia-settings linux-headers
fi

# ── 32-bit libraries ─────────────────────────────────────
if [ "$m32" = "y" ] || [ "$m32" = "Y" ]; then
  echo "==> 32-bit libraries"
  install lib32-mesa lib32-vulkan-radeon lib32-alsa-lib lib32-alsa-plugins
  [ "$nv" = "y" ] || [ "$nv" = "Y" ] && install lib32-nvidia-utils
fi

# ── Wine ─────────────────────────────────────────────────
if [ "$wine" = "y" ] || [ "$wine" = "Y" ]; then
  echo "==> Wine"
  install wine-staging winetricks
fi

# ── umu-launcher ─────────────────────────────────────────
if [ "$um" = "y" ] || [ "$um" = "Y" ]; then
  echo "==> umu-launcher"
  install umu-launcher
fi

# ── Gaming tools ─────────────────────────────────────────
if [ "$st" = "y" ] || [ "$st" = "Y" ]; then
  echo "==> Steam + Lutris + MangoHud + GameMode"
  install gamemode lib32-gamemode mangohud lib32-mangohud steam lutris
fi

# ── Gamepad ──────────────────────────────────────────────
if [ "$gp" = "y" ] || [ "$gp" = "Y" ]; then
  echo "==> Gamepad support"
  install steam-devices
fi

# ── asusctl ──────────────────────────────────────────────
if [ "$ac" = "y" ] || [ "$ac" = "Y" ]; then
  echo "==> Building asusctl..."
  install rust cargo clang
  rm -rf /tmp/asusctl
  git clone --depth 1 https://github.com/opengamingcollective/asusctl /tmp/asusctl
  (cd /tmp/asusctl && CARGO_BUILD_JOBS=1 cargo build --release -p asusctl -p asusd)
  sudo cp /tmp/asusctl/target/release/asusctl /usr/local/bin/ 2>/dev/null || true
  sudo cp /tmp/asusctl/target/release/asusd /usr/local/bin/ 2>/dev/null || true
  sudo cp /tmp/asusctl/data/asusd.service /etc/systemd/system/ 2>/dev/null || true
  sudo systemctl enable --now asusd 2>/dev/null || true
  rm -rf /tmp/asusctl
fi

echo ""
echo "============================================"
echo "  Gaming setup complete!"
[ "$st" = "y" ] || [ "$st" = "Y" ] && echo "  Steam: gamemoderun mangohud %command%"
[ "$um" = "y" ] || [ "$um" = "Y" ] && echo "  umu:    gamemoderun umu-run game.exe"
[ "$ac" = "y" ] || [ "$ac" = "Y" ] && echo "  asus:   asusctl profile -p Performance"
echo "============================================"
