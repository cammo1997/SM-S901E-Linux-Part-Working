#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 cammo1997 & contributors
#
# Install and configure Waydroid (Android 13 / LineageOS 20 ARM64) on the
# Samsung Galaxy S22 (sm8450-samsung-r0q) running postmarketOS / Alpine edge.
#
# Prerequisites:
#   - Kernel built with CONFIG_ANDROID_BINDER_IPC=y and CONFIG_ANDROID_BINDERFS=y
#     (included in kernel/r0q.fragment & kernel/config-r0q.aarch64).
#
# Usage on phone (as root):
#   sudo sh tools/s22-waydroid-setup.sh [--vanilla]
#
# Defaults to GAPPS (includes Google Play Store). Pass --vanilla for AOSP/LineageOS without GAPPS.

set -eu

REPO="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PC="$REPO/phone-config"
SYSTEM_TYPE="GAPPS"

while [ $# -gt 0 ]; do
	case "$1" in
		--vanilla)
			SYSTEM_TYPE="VANILLA"
			shift
			;;
		--gapps)
			SYSTEM_TYPE="GAPPS"
			shift
			;;
		*)
			echo "Usage: $0 [--gapps|--vanilla]" >&2
			exit 1
			;;
	esac
done

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root: sudo sh $0" >&2
	exit 1
fi

if ! grep -qw binder /proc/filesystems; then
	echo "ERROR: binderfs is not supported by the running kernel." >&2
	echo "Rebuild and flash boot.img with CONFIG_ANDROID_BINDER_IPC=y and CONFIG_ANDROID_BINDERFS=y." >&2
	exit 1
fi

echo "==> Installing Waydroid packages..."
apk add waydroid waydroid-systemd waydroid-nftrules plasma-mobile-waydroid lxc nftables dnsmasq

# S22 uses libssc/iio-sensor-proxy rather than sensorfwd; waydroid-sensors crashes
# (__glibcxx_assert_fail) and stalls Android SystemServer boot if installed.
if apk info -e waydroid-sensors >/dev/null 2>&1 || apk info -e sensorfw >/dev/null 2>&1; then
	echo "==> Removing incompatible waydroid-sensors / sensorfw packages..."
	apk del waydroid-sensors waydroid-sensors-systemd sensorfw sensorfw-systemd 2>/dev/null || true
fi

echo "==> Installing nftables firewall & NAT rules for waydroid0..."
install -D -m 0644 "$PC/etc/nftables.d/90-s22-waydroid.nft" /etc/nftables.d/90-s22-waydroid.nft
systemctl enable --now nftables 2>/dev/null || true
nft -f /etc/nftables.nft 2>/dev/null || systemctl reload nftables 2>/dev/null || true

echo "==> Initializing Waydroid ($SYSTEM_TYPE)..."
waydroid init -s "$SYSTEM_TYPE" -f

echo "==> Enabling waydroid-container.service..."
systemctl enable --now waydroid-container.service

# Enable multi-window integration with Plasma Mobile / KWin
waydroid prop set persist.waydroid.multi_windows true 2>/dev/null || true

echo ""
echo "Waydroid ($SYSTEM_TYPE) installed and configured!"
echo "Start a session from your user account (or via the Waydroid app icon):"
echo "  waydroid session start &"
echo "  waydroid show-full-ui"
echo ""
echo "If using GAPPS and Play Protect asks to register the device, run:"
echo "  sudo waydroid shell -- sh -c 'ANDROID_RUNTIME_ROOT=/apex/com.android.runtime ANDROID_DATA=/data ANDROID_TZDATA_ROOT=/apex/com.android.tzdata ANDROID_I18N_ROOT=/apex/com.android.i18n sqlite3 /data/data/com.google.android.gsf/databases/gservices.db \"select * from main where name = \\\"android_id\\\";\"'"
echo "and register the ID at https://www.google.com/android/uncertified/"
