#!/usr/bin/env bash
# One-time PC setup (Ubuntu / Pop!_OS 24.04 tested). Run it ONCE before starting; it needs sudo for apt and group changes.
#   bash tools/setup-host.sh
set -euo pipefail

echo "[1/4] Installing packages (sudo)"
sudo apt-get update
sudo apt-get install -y \
  adb android-sdk-platform-tools-common udev \
  git rsync make build-essential bc flex bison libssl-dev libelf-dev libncurses-dev openssl \
  gcc-aarch64-linux-gnu device-tree-compiler zstd lz4 cpio \
  python3 python3-venv python3-tk pipx libusb-1.0-0 \
  network-manager e2fsprogs patch curl kmod pciutils usbmuxd

echo "[2/4] pmbootstrap via pipx (tested: 3.12.1)"
pipx install pmbootstrap || pipx upgrade pmbootstrap
pipx ensurepath

echo "[3/4] USB permissions (log out and in again afterwards)"
sudo usermod -aG plugdev "$USER" || true

echo "[4/4] ModemManager grabs the phone's USB device during flashing; stop it for the session (we also stop it before every flash)"
sudo systemctl stop ModemManager || true

cat <<MSG

Done. Next: docs/SETUP.md (pmbootstrap init with Plasma Mobile), then docs/flashing-workflow.md.
Keep the kernel/source folders in a path WITHOUT spaces (the kernel build refuses spaces), e.g. ~/s22-kernel.
MSG
