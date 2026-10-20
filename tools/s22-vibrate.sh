#!/bin/sh
# Play a CS40L26 ROM haptic effect on the Galaxy S22 (r0q) without a kernel driver (attempt 33 DT: i2c9 + reset).
# Usage: sudo s22-vibrate.sh [effect 0-38]   (default 0)
# Registers from Cirrus's CS40L26 driver: DSP virtual mailbox 0x00013020.
set -e
E=${1:-0}
BUS=$(for b in /sys/bus/i2c/devices/i2c-*; do readlink -f $b/of_node | grep -q 'i2c@a84000' && basename $b | cut -d- -f2; done)
[ -n "$BUS" ] || { echo "i2c@a84000 not found (needs attempt 33 DT)"; exit 1; }
I="i2ctransfer -y $BUS"
$I w4@0x40 0 0 0 0 r4 >/dev/null 2>&1 || true            # first access wakes the chip from hibernation (NACK expected)
$I w8@0x40 0x00 0x01 0x30 0x20 0x02 0x00 0x00 0x03      # prevent hibernation
sleep 0.05
$I w8@0x40 0x00 0x01 0x30 0x20 0x01 0x80 0x00 $(printf 0x%02x "$E")   # ROM bank effect
