#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 cammo1997 & contributors
#
# Install Samsung Galaxy S22 (r0q) userspace USB-C Rp charger current-limit
# sync helpers (udev + systemd/OpenRC) and mobile UPower low-battery thresholds.
# Run as root on the phone: sudo sh tools/s22-battery-charger-setup.sh

set -eu

REPO="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PC="$REPO/phone-config"

install -D -m 0755 "$PC/usr/local/sbin/s22-usbc-ilim.sh" /usr/local/sbin/s22-usbc-ilim.sh
install -D -m 0644 "$PC/etc/udev/rules.d/99-s22-max77705-ilim.rules" /etc/udev/rules.d/99-s22-max77705-ilim.rules

if [ -d /run/systemd/system ] || command -v systemctl >/dev/null 2>&1; then
	install -D -m 0644 "$PC/etc/systemd/system/s22-usbc-ilim.service" /etc/systemd/system/s22-usbc-ilim.service
	systemctl daemon-reload 2>/dev/null || true
	systemctl enable --now s22-usbc-ilim.service 2>/dev/null || true
fi

if command -v rc-update >/dev/null 2>&1; then
	install -D -m 0755 "$PC/etc/init.d/s22-usbc-ilim" /etc/init.d/s22-usbc-ilim
	rc-update add s22-usbc-ilim default 2>/dev/null || true
fi

if command -v udevadm >/dev/null 2>&1; then
	udevadm control --reload 2>/dev/null || true
fi
/usr/local/sbin/s22-usbc-ilim.sh || true

if [ -f /etc/UPower/UPower.conf ]; then
	if [ ! -f /etc/UPower/UPower.conf.orig ]; then
		cp -a /etc/UPower/UPower.conf /etc/UPower/UPower.conf.orig
	fi
	sed -i \
		-e 's/^PercentageLow=.*/PercentageLow=10/' \
		-e 's/^PercentageCritical=.*/PercentageCritical=2/' \
		-e 's/^PercentageAction=.*/PercentageAction=1/' \
		/etc/UPower/UPower.conf
	if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
		systemctl restart upower.service 2>/dev/null || true
	fi
fi

echo "S22 battery/charger userspace helpers installed successfully."
