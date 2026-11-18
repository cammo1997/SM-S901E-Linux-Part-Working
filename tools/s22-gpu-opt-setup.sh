#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 cammo1997 & contributors
#
# Install Samsung Galaxy S22 (sm8450-samsung-r0q) Adreno 730 GPU modprobe
# tuning (`s22-gpu.conf`) and optional rebuilt `msm.ko.zst` (msm-w10),
# `libvulkan_freedreno.so` (Turnip W5), and `libgallium-*.so` artifacts.
#
# Usage on phone:
#   sudo sh tools/s22-gpu-opt-setup.sh [--msm /path/to/msm.ko.zst] \
#        [--turnip /path/to/libvulkan_freedreno.so] \
#        [--gallium /path/to/libgallium-26.3.0.so]

set -eu

REPO="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PC="$REPO/phone-config"
MSM_KO=""
TURNIP_SO=""
GALLIUM_SO=""

while [ $# -gt 0 ]; do
	case "$1" in
		--msm)
			MSM_KO="$2"
			shift 2
			;;
		--turnip)
			TURNIP_SO="$2"
			shift 2
			;;
		--gallium)
			GALLIUM_SO="$2"
			shift 2
			;;
		*)
			echo "Unknown argument: $1" >&2
			exit 1
			;;
	esac
done

install -D -m 0644 "$PC/etc/modprobe.d/s22-gpu.conf" /etc/modprobe.d/s22-gpu.conf

if [ -n "$MSM_KO" ] && [ -f "$MSM_KO" ]; then
	KVER="$(uname -r)"
	DST="/lib/modules/$KVER/kernel/drivers/gpu/drm/msm/msm.ko.zst"
	if [ -f "$DST" ] && [ ! -f "$DST.orig" ]; then
		cp -a "$DST" "$DST.orig"
	fi
	install -D -m 0644 "$MSM_KO" "$DST"
	depmod -a "$KVER" 2>/dev/null || true
	echo "Installed kernel GPU module: $DST (reboot required; never hot-unbind msm-mdss)."
fi

if [ -n "$TURNIP_SO" ] && [ -f "$TURNIP_SO" ]; then
	ICD="/usr/lib/libvulkan_freedreno.so"
	if [ -f "$ICD" ] && [ ! -f "$ICD.orig" ]; then
		cp -a "$ICD" "$ICD.orig"
	fi
	install -D -m 0755 "$TURNIP_SO" "$ICD"
	echo "Installed Turnip Vulkan ICD: $ICD"
fi

if [ -n "$GALLIUM_SO" ] && [ -f "$GALLIUM_SO" ]; then
	GAL="/usr/lib/$(basename "$GALLIUM_SO")"
	if [ -f "$GAL" ] && [ ! -f "$GAL.orig" ]; then
		cp -a "$GAL" "$GAL.orig"
	fi
	install -D -m 0755 "$GALLIUM_SO" "$GAL"
	echo "Installed Gallium megadriver: $GAL"
fi

echo "S22 Adreno 730 GPU configuration installed."
