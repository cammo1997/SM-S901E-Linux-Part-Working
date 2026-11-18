#!/bin/sh
# SPDX-License-Identifier: MIT
# Sync MAX77705 charger input_current_limit with Type-C Rp current_max ([C] ports)
# on Samsung Galaxy S22 (sm8450-samsung-r0q).

USBC="/sys/class/power_supply/max77705-usbc"
CHG="/sys/class/power_supply/max77705-charger"
[ -d "$USBC" ] && [ -d "$CHG" ] || exit 0

online=$(cat "$USBC/online" 2>/dev/null || echo 0)
if [ "$online" != "1" ]; then
	exit 0
fi

usb_type=$(cat "$USBC/usb_type" 2>/dev/null || echo "")
cmax=$(cat "$USBC/current_max" 2>/dev/null || echo 0)
ilim=$(cat "$CHG/input_current_limit" 2>/dev/null || echo 0)

# When attached via non-PD Type-C Rp ([C]), ensure CHGIN_ILIM matches Rp
# (1.5 A / 3.0 A) instead of staying at the 900 mA bootloader default.
case "$usb_type" in
	*"[C]"*)
		if [ "$cmax" -ge 500000 ] && [ "$ilim" != "$cmax" ]; then
			echo "$cmax" > "$CHG/input_current_limit" 2>/dev/null || true
		fi
		;;
esac
