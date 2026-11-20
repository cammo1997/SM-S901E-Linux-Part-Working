#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 cammo1997 & contributors
#
# Install Samsung Galaxy S22 (sm8450-samsung-r0q) Plasma Mobile UI smoothness
# fixes:
#   - KWIN_DISABLE_UDMABUF_IMPORT=1 in s22-kwin-gpu.sh and kwin_wayland drop-in
#   - Async 60 ms applyTimer in the Dark Mode QuickSettings tile to avoid
#     blocking the plasmashell render thread on theme switches
#
# Run as root on the phone:
#   sudo sh tools/s22-ui-smoothness-setup.sh

set -eu

REPO="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PC="$REPO/phone-config"

install -D -m 0755 "$PC/etc/profile.d/s22-kwin-gpu.sh" /etc/profile.d/s22-kwin-gpu.sh
install -D -m 0644 "$PC/etc/systemd/user/plasma-kwin_wayland.service.d/s22-adreno.conf" \
	/etc/systemd/user/plasma-kwin_wayland.service.d/s22-adreno.conf

DARK_DST="/usr/share/plasma/quicksettings/org.kde.plasma.quicksetting.darkmode/contents/ui/main.qml"
if [ -f "$DARK_DST" ] && [ ! -f "$DARK_DST.orig" ]; then
	cp -a "$DARK_DST" "$DARK_DST.orig"
fi
install -D -m 0644 "$PC$DARK_DST" "$DARK_DST"

echo "S22 UI smoothness & dark-mode toggle configuration installed."
