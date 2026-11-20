#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 cammo1997 & contributors
#
# Install Steam (x86/x86_64 via FEX-Emu ARMv8.4 + Ubuntu 24.04 ARM64 Distrobox)
# with Turnip Adreno 730 GPU acceleration on Samsung Galaxy S22 (sm8450-samsung-r0q)
# running postmarketOS / Alpine edge.
#
# Why Distrobox + Ubuntu 24.04?
#   postmarketOS uses musl libc; FEX-Emu and Steam's runtime require glibc +
#   official FEX ARMv8.4 packages. Rootless Podman + Distrobox runs an Ubuntu 24.04
#   ARM64 container sharing $HOME, Wayland/Xwayland, PulseAudio, and /dev/dri.
#
# Usage on phone (as root):
#   sudo sh tools/s22-steam-setup.sh [username]

set -eu

REPO="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PC="$REPO/phone-config"
TARGET_USER="${1:-${SUDO_USER:-}}"

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root: sudo sh $0 [username]" >&2
	exit 1
fi

if [ -z "$TARGET_USER" ] || ! id "$TARGET_USER" >/dev/null 2>&1; then
	echo "ERROR: Specify a valid non-root user: sudo sh $0 <username>" >&2
	exit 1
fi

TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"

echo "==> Installing host dependencies (Xwayland, rootless Podman, Distrobox)..."
# Note: xwayland is required both for Steam (X11/XWayland) and so KWin --xwayland
# does not leave /tmp/.X11-unix/X0 unaccepted (which can stall X11-aware clients).
apk add xwayland podman distrobox fuse-overlayfs squashfuse shadow-uidmap curl

echo "==> Enabling overlay & fuse kernel modules..."
install -D -m 0644 "$PC/etc/modules-load.d/s22-containers.conf" /etc/modules-load.d/s22-containers.conf
modprobe overlay 2>/dev/null || true
modprobe fuse 2>/dev/null || true

echo "==> Configuring rootless subuid/subgid for $TARGET_USER..."
grep -q "^${TARGET_USER}:" /etc/subuid 2>/dev/null || echo "${TARGET_USER}:100000:65536" >> /etc/subuid
grep -q "^${TARGET_USER}:" /etc/subgid 2>/dev/null || echo "${TARGET_USER}:100000:65536" >> /etc/subgid

echo "==> Installing /usr/local/bin/s22-steam launcher..."
install -D -m 0755 "$PC/usr/local/bin/s22-steam" /usr/local/bin/s22-steam

echo "==> Creating Ubuntu 24.04 ARM64 distrobox (steam-box) for $TARGET_USER..."
su - "$TARGET_USER" -c '
set -eu
if ! podman container exists steam-box 2>/dev/null; then
	distrobox create --yes --name steam-box --image docker.io/library/ubuntu:24.04
fi

distrobox enter steam-box -- bash -euo pipefail -c "
	sudo apt-get update
	sudo apt-get install -y software-properties-common curl ca-certificates gnupg \
		mesa-vulkan-drivers libgl1-mesa-dri libegl-mesa0 vulkan-tools \
		zenity xdg-utils squashfs-tools libpulse0
	sudo add-apt-repository -y ppa:fex-emu/fex
	sudo apt-get update
	sudo apt-get install -y fex-emu-armv8.4 fex-emu-wine || sudo apt-get install -y fex-emu-armv8.4

	if [ ! -d \"\$HOME/.local/share/fex-emu/RootFS/Ubuntu_24_04\" ]; then
		echo \"==> Downloading FEX Ubuntu 24.04 x86/x86_64 RootFS...\"
		mkdir -p \"\$HOME/.local/share/fex-emu/RootFS\" \"\$HOME/.config/fex-emu\"
		FEXRootFSFetcher -y -x --distro-name=ubuntu --distro-version=24.04 --distro-variant=default || true
	fi

	if [ ! -f \"\$HOME/.config/fex-emu/Config.json\" ]; then
		mkdir -p \"\$HOME/.config/fex-emu\"
		printf '\''{\"Config\":{\"RootFS\":\"Ubuntu_24_04\"},\"ThunksDB\":{}}\n'\'' > \"\$HOME/.config/fex-emu/Config.json\"
	fi

	if ! dpkg -s steam-launcher >/dev/null 2>&1; then
		curl -fSL https://cdn.fastly.steamstatic.com/client/installer/steam.deb -o /tmp/steam-launcher.deb
		sudo dpkg -i /tmp/steam-launcher.deb || sudo apt-get install -f -y
		rm -f /tmp/steam-launcher.deb
	fi
	sudo apt-get clean
"

mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/steam.desktop" << DESK
[Desktop Entry]
Name=Steam
Comment=Application for managing and playing games on Steam (via FEX-Emu)
Exec=/usr/local/bin/s22-steam %U
Icon=steam
Terminal=false
Type=Application
Categories=Network;FileTransfer;Game;
MimeType=x-scheme-handler/steam;x-scheme-handler/steamlink;
StartupNotify=true
DESK
'

echo ""
echo "Steam (FEX-Emu + Ubuntu 24.04 Distrobox) installed for $TARGET_USER!"
echo "If Xwayland was just installed, reboot or restart plasma-mobile.service before launching Steam."
echo "Launch from the app launcher (Steam) or run: s22-steam"
