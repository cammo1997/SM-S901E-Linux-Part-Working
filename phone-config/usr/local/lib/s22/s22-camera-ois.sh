#!/bin/sh
# Initialize and center the Samsung Galaxy S22 (r0q) OIS MCU (0x62 on 88c000.i2c).
# Sequence matches stock CamX dual-OIS init + centering settings.
set -eu

for c in /sys/bus/i2c/devices/*-0062/of_node/compatible \
	 /sys/firmware/devicetree/base/soc@0/geniqup@8c0000/i2c@88c000/ois@62/compatible; do
	if [ -e "$c" ] && grep -qs "samsung,r0q-ois" "$c"; then
		echo "OIS 0x62 bound in device tree (managed by samsung-r0q-ois kernel driver)"
		exit 0
	fi
done

for d in /sys/bus/i2c/devices/*-0062/driver; do
	if [ -e "$d" ]; then
		echo "OIS 0x62 managed by kernel driver ($(basename "$(readlink -f "$d")"))"
		exit 0
	fi
done

modprobe i2c-dev 2>/dev/null || true

BUS=""
for _ in $(seq 1 10); do
	for d in /sys/bus/i2c/devices/i2c-*; do
		[ -e "$d" ] || continue
		target="$(readlink -f "$d" 2>/dev/null || true)"
		case "$target" in
			*88c000.i2c*)
				BUS="${d##*/i2c-}"
				break
				;;
		esac
	done
	[ -n "$BUS" ] && [ -e "/dev/i2c-$BUS" ] && break
	sleep 0.5 2>/dev/null || usleep 500000 2>/dev/null || sleep 1
done

[ -n "$BUS" ] || BUS=1
[ -e "/dev/i2c-$BUS" ] || exit 1

I2CTRANSFER="$(command -v i2ctransfer || echo /usr/sbin/i2ctransfer)"

for ch in 0x01 0x03 0x02 0x03; do
	"$I2CTRANSFER" -y "$BUS" w3@0x62 0x00 0xbe "$ch"
done
"$I2CTRANSFER" -y "$BUS" w3@0x62 0x00 0x00 0x01
sleep 0.05 2>/dev/null || usleep 50000 2>/dev/null || sleep 1
"$I2CTRANSFER" -y "$BUS" w3@0x62 0x00 0x02 0x05
"$I2CTRANSFER" -y "$BUS" w3@0x62 0x00 0x00 0x01
