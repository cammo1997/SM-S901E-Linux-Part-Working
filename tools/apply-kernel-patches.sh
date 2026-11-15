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
	"$U/local-sm8450-camss-cphy.patch" \
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
	"$U/local-sm8450-qmp-combo-usb3-tuning.patch" \
	"$U/local-dwc3-qcom-role-vbus-override.patch" \
	"$U/local-max77705-typec-dp-altmode.patch" \
	"$U/local-sm8450-ipa-v5.1.patch" \
	"$U/local-ak7375-s22-vcm.patch" \
	; do
	echo "  $(basename "$p")"
	patch -p1 --forward < "$p"
done

echo "[2/5] Installing touchscreen fsleep fix, S6E3FAC panel driver, and r0q DTS..."
cp "$U/fts2ba61y.c.as-built" drivers/input/touchscreen/fts2ba61y.c
cp "$R/panel-samsung-s6e3fac.c" drivers/gpu/drm/panel/panel-samsung-s6e3fac.c
cp "$R/panel-samsung-s6e3fac-mdnie.h" drivers/gpu/drm/panel/panel-samsung-s6e3fac-mdnie.h
cp "$R/sm8450-samsung-r0q.dts" arch/arm64/boot/dts/qcom/sm8450-samsung-r0q.dts
for f in imx374.c imx374-regs.h s5k3k1.c s5k3k1-regs.h imx563.c imx563-regs.h s5kgn3.c s5kgn3-regs.h samsung-r0q-ois.c; do
    cp "$R/$f" "drivers/media/i2c/$f"
done

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

python3 - <<'CAMWIRE'
from pathlib import Path
kp = Path("drivers/media/i2c/Kconfig")
kc = kp.read_text()
if "config VIDEO_IMX374" not in kc:
    kc = kc.replace("config VIDEO_IMX412", """config VIDEO_IMX374
	tristate "Sony IMX374 sensor support"
	select V4L2_CCI_I2C
	help
	  This is a Video4Linux2 sensor driver for the Sony
	  IMX374 camera.

config VIDEO_IMX412""", 1)
if "config VIDEO_IMX563" not in kc:
    kc = kc.replace("config VIDEO_IR_I2C", """config VIDEO_IMX563
	tristate "Sony IMX563 sensor support"
	select V4L2_CCI_I2C
	help
	  This is a Video4Linux2 sensor driver for the Sony
	  IMX563 camera.

config VIDEO_IR_I2C""", 1)
if "config VIDEO_S5K3K1" not in kc:
    kc = kc.replace("config VIDEO_S5K5BAF", """config VIDEO_S5K3K1
	tristate "Samsung S5K3K1 sensor support"
	select V4L2_CCI_I2C
	help
	  This is a V4L2 sensor driver for Samsung S5K3K1 camera sensor.

config VIDEO_S5KGN3
	tristate "Samsung S5KGN3 sensor support"
	select V4L2_CCI_I2C
	help
	  This is a V4L2 sensor driver for Samsung S5KGN3 camera sensor.

config VIDEO_SAMSUNG_R0Q_OIS
	tristate "Samsung Galaxy S22 (r0q) OIS MCU support"
	depends on I2C && GPIOLIB
	help
	  Runtime PM and centering driver for the Galaxy S22 (r0q) OIS MCU.

config VIDEO_S5K5BAF""", 1)
kp.write_text(kc)

mp = Path("drivers/media/i2c/Makefile")
mk = mp.read_text()
for anchor, add in [
    ("obj-$(CONFIG_VIDEO_IMX355) += imx355.o\n", "obj-$(CONFIG_VIDEO_IMX374) += imx374.o\n"),
    ("obj-$(CONFIG_VIDEO_IMX415) += imx415.o\n", "obj-$(CONFIG_VIDEO_IMX563) += imx563.o\n"),
    ("obj-$(CONFIG_VIDEO_S5K3M5) += s5k3m5.o\n", "obj-$(CONFIG_VIDEO_S5K3K1) += s5k3k1.o\n"),
    ("obj-$(CONFIG_VIDEO_S5K6A3) += s5k6a3.o\n", "obj-$(CONFIG_VIDEO_S5KGN3) += s5kgn3.o\n"),
    ("obj-$(CONFIG_VIDEO_AK7375) += ak7375.o\n", "obj-$(CONFIG_VIDEO_SAMSUNG_R0Q_OIS) += samsung-r0q-ois.o\n"),
]:
    if add not in mk:
        mk = mk.replace(anchor, anchor + add, 1)
mp.write_text(mk)
CAMWIRE

echo "[4/5] Merging kernel/config-r0q.aarch64 + kernel/r0q.fragment..."
cp "$R/config-r0q.aarch64" .config
scripts/kconfig/merge_config.sh -m .config "$R/r0q.fragment"
scripts/config --set-str INITRAMFS_SOURCE ""
scripts/config --set-str CMDLINE "clk_ignore_unused pd_ignore_unused ignore_loglevel console=tty0 panic=0 log_buf_len=4M"
scripts/config --disable LOCALVERSION_AUTO
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""

echo "[5/5] Running make olddefconfig..."
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig </dev/null
echo "Done. Ready for: make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j\$(nproc) Image.gz modules qcom/sm8450-samsung-r0q.dtb"
