#!/bin/bash
# SPDX-License-Identifier: MIT
# Apply all r0q mainline Linux 7.2.11 patches, panel driver, DTS, and config.
# Usage: bash tools/apply-kernel-patches.sh /path/to/linux-7.2.11

set -euo pipefail

if [ $# -lt 1 ]; then
	echo "Usage: $0 /path/to/linux-7.2.11" >&2
	exit 1
fi

K="$(cd "$1" && pwd)"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
R="$REPO/kernel"
U="$R/upstream-patches"
T="$U/tabs8"

cd "$K"

echo "[1/5] Applying kernel patches..."
for p in \
	"$U/0001-PCI-qcom-Parse-iommu-map-entries-using-iommu-cells.patch" \
	"$U/fts-v1-1-binding.patch" \
	"$U/fts-v1-2-driver.patch" \
	"$U/local-ath11k-wcn6855-hw21-coldboot-cal.patch" \
	"$U/local-cs35l41-right-channel-amp.patch" \
	"$U/local-q6apm-mi2s-start-bclk-at-prepare.patch" \
	"$U/max77705-v1-support-rev2.patch" \
	"$T/sm8450-camss.patch" \
	"$T/camcc-sm8450-shared-rcg.patch" \
	"$T/camcc-sm8450-retain-ff.patch" \
	"$T/sc8280xp-mi2s-codec-fmt-sysclk.patch" \
	"$U/local-sc8280xp-cs35l41-pll-reference.patch" \
	"$T/max17042-max77705-i2c-id.patch" \
	"$T/max77705-charger-otg-regulator.patch" \
	"$T/max77705-charger-battery-info.patch" \
	"$T/max77705-charger-otg-shutdown.patch" \
	"$T/max77705-charger-aicl-bounded.patch" \
	"$T/max77705-charger-aicl-notify.patch" \
	"$T/max77705-charger-review-fixes.patch" \
	"$T/max77705-typec.patch" \
	"$T/max77705-typec-source-attach.patch" \
	"$T/max77705-typec-prefer-voltage.patch" \
	"$T/max77705-typec-review-fixes.patch" \
	"$T/dwc3-host-power-lost.patch" \
	"$U/local-sm8450-ipa-v5.1.patch" \
	; do
	echo "  $(basename "$p")"
	patch -p1 --forward < "$p"
done

echo "[2/5] Installing touchscreen fsleep fix, S6E3FAC panel driver + mDNIe tables, and r0q DTS..."
cp "$U/fts2ba61y.c.as-built" drivers/input/touchscreen/fts2ba61y.c
cp "$R/panel-samsung-s6e3fac.c" drivers/gpu/drm/panel/panel-samsung-s6e3fac.c
cp "$R/panel-samsung-s6e3fac-mdnie.h" drivers/gpu/drm/panel/panel-samsung-s6e3fac-mdnie.h
cp "$R/sm8450-samsung-r0q.dts" arch/arm64/boot/dts/qcom/sm8450-samsung-r0q.dts

echo "[3/5] Registering DRM_PANEL_SAMSUNG_S6E3FAC in Kconfig & Makefile..."
python3 - <<'PY'
from pathlib import Path

k = Path('drivers/gpu/drm/panel/Kconfig')
s = k.read_text()
anchor = 'config DRM_PANEL_SAMSUNG_S6E3HA8\n'
entry = '''config DRM_PANEL_SAMSUNG_S6E3FAC
\ttristate "Samsung S6E3FAC DSI command mode panel"
\tdepends on GPIOLIB
\tdepends on OF
\tdepends on DRM_MIPI_DSI
\tdepends on BACKLIGHT_CLASS_DEVICE
\tselect DRM_DISPLAY_HELPER
\tselect DRM_DISPLAY_DSC_HELPER
\thelp
\t  Say Y or M here if you want to enable support for the
\t  Samsung S6E3FAC DDIC with the AMB606AW01 1080x2340 AMOLED
\t  panel found in the Samsung Galaxy S22 (r0q).

'''
if 'config DRM_PANEL_SAMSUNG_S6E3FAC' not in s:
    k.write_text(s.replace(anchor, entry + anchor, 1))

m = Path('drivers/gpu/drm/panel/Makefile')
ms = m.read_text()
m_anchor = 'obj-$(CONFIG_DRM_PANEL_SAMSUNG_S6E3HA2) += panel-samsung-s6e3ha2.o\n'
line = 'obj-$(CONFIG_DRM_PANEL_SAMSUNG_S6E3FAC) += panel-samsung-s6e3fac.o\n'
if line not in ms:
    m.write_text(ms.replace(m_anchor, m_anchor + line, 1))

tm = Path('drivers/usb/typec/Makefile')
ts = tm.read_text()
old_t = 'obj-$(CONFIG_TYPEC_MAX77705)\t+= max77705.o\n'
new_t = 'obj-$(CONFIG_TYPEC_MAX77705)\t+= max77705_typec.o\nmax77705_typec-y\t\t:= max77705.o\n'
if old_t in ts:
    tm.write_text(ts.replace(old_t, new_t, 1))
PY

echo "[4/5] Merging kernel/config-r0q.aarch64 + kernel/r0q.fragment..."
cp "$R/config-r0q.aarch64" .config
scripts/kconfig/merge_config.sh -m .config "$R/r0q.fragment"

echo "[5/5] Running make olddefconfig..."
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig </dev/null
echo "Done. Ready for: make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j\$(nproc) Image.gz modules qcom/sm8450-samsung-r0q.dtb"
