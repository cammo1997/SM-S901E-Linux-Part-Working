#!/bin/sh
# SPDX-License-Identifier: MIT
# Ensure usb0 keeps 172.16.42.1/16 UP and unudhcpd@usb0 responds immediately on replug.
IFACE="${1:-usb0}"
[ -d "/sys/class/net/$IFACE" ] || exit 0
ip link set "$IFACE" up 2>/dev/null || true
if ! ip -4 addr show dev "$IFACE" | grep -q '172.16.42.1'; then
    ip addr add 172.16.42.1/16 dev "$IFACE" 2>/dev/null || true
fi
systemctl restart "unudhcpd@${IFACE}.service" 2>/dev/null || true
