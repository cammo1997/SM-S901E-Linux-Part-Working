# Samsung Galaxy S22 (SM-S901E, r0q) – Mainline Linux / postmarketOS port

**Keywords:** r0q · samsung-r0q · SM-S901E · SM-S901B · Galaxy S22 · SM8450 · Snapdragon 8 Gen 1 · waipio · mainline Linux · postmarketOS · Plasma Mobile · uniLoader

Status: boots, USB, Wi-Fi 5 GHz, Bluetooth, 120 Hz display, stereo speakers, mics, buttons work. See [CHECKLIST](CHECKLIST.md) and [what fixed what](docs/WHAT-FIXED-WHAT.md).
Credits: [CREDITS.md](CREDITS.md) · License: [LICENSE.md](LICENSE.md) (GPL-2.0 kernel parts, MIT the rest)

## r0q kernels compared

| Kernel | Source | Status |
|---|---|---|
| 5.10 (Android GKI) | Samsung stock kernel ([opensource.samsung.com](https://opensource.samsung.com)) | Android only, not mainline |
| 6.16-rc1 | Ghatto404 / Arthurus36 ([Nura wiki](https://wiki.nura.eco/wiki/Samsung_Galaxy_S22_5G_(samsung-r0q))) | Wiki lists Wi-Fi as working; source tree not public (Arthurus36's public repo has a decompiled stock DT only) |
| 7.2.11 (upstream) | Mainline `arch/arm64/boot/dts/qcom/sm8450-samsung-r0q.dts` | Minimal: framebuffer, regulators, USB 2.0. No panel, touch, Wi-Fi, GPU or battery |
| **7.2.11 (this repo)** | This port | Wi-Fi 5 GHz, Bluetooth, 120 Hz panel, touch, GPU, stereo speakers, mics, USB-C hotplug, MTP, buttons, charging, modem boot |

To my knowledge this is the newest working mainline kernel on the r0q, and the most complete public one,
as of 2026-10-18. Know of a newer or more complete one? Please open an issue.

# Galaxy S22 (SM-S901E / r0q) - mainline Linux + postmarketOS (part working)

Running mainline Linux 7.2.11 + postmarketOS (Plasma Mobile) on a **Samsung Galaxy S22, SM-S901E** (Snapdragon 8 Gen 1 / SM8450,
Australian XSA, One UI 7, bootloader unlocked) from the phone's own storage. Android removed. Work in progress.

> Source, configs, notes and logs only. No firmware, images, backups or Samsung blobs are in this repo.

## Status (2026-10-20, attempt 28)

| Works | Partly | Not yet |
|---|---|---|
| Boot via uniLoader, UFS, USB networking + SSH | 2.4 GHz Wi-Fi association (5 GHz works) | Calls/SMS, mobile data (no sm8450 IPA in mainline) |
| **Wi-Fi** 5 GHz, Wi-Fi 6 2x2, 960 Mbit/s, autoconnects at boot | **Modem**: boots, QMI online; no SIM test yet | Sleep/suspend, sensors, cameras, fingerprint, NFC |
| **Audio: stereo speakers (2x CS35L41) + 2 mics**, Speaker/Mic in Plasma | **USB OTG: USB stick detected in Dolphin; ISO/exFAT sticks need kernel support** | **NPU userspace (CDSP firmware runs, attempt 29)**, speaker protection |
| **USB-C hotplug** (MAX77705, attempt 28) + **MTP file transfer to PC** | On-screen keyboard (no auto-hide) | Writable RTC |
| **Display 120 Hz** (new mainline S6E3FAC panel driver) | **Vulkan (Turnip) on Adreno 730** | |
| Touch (STM FTS2BA61Y, multitouch), GPU (Adreno 730) | | |
| Bluetooth (address from EFS), battery %, charging | | |
| **6.8 GiB RAM**, firewall (nftables) | | |
| Plasma Mobile autologin (tinydm), **power/volume up/volume down keys**, clock restore | | |

**Complete start-to-finish guide (unlock, root, EFS backup, TWRP, everything we found): [docs/S22-Mainline-Linux-Guide.pdf](docs/S22-Mainline-Linux-Guide.pdf).**

**New to this? Start with [docs/SETUP.md](docs/SETUP.md)** (dependencies, EFS backups, TWRP, Plasma Mobile image) **then [docs/flashing-workflow.md](docs/flashing-workflow.md)** (TWRP + adb flash/reboot loop).

What we did, step by step: [docs/CHANGELOG.md](docs/CHANGELOG.md).

Full checklist: [docs/CHECKLIST-not-sorted.md](docs/CHECKLIST-not-sorted.md). Complete attempt-by-attempt log: [docs/boot-log.md](docs/boot-log.md).

## What actually helped (read this first)

1. **Use Samsung's own S901E kernel source as the first reference.** It is public (opensource.samsung.com, mirrored on GitHub as
   `ykasidit/s22_kernel_SM-S901E`, `polygraphene/SM-S901U_NA_12_Kernel`). The panel stayed black for an hour with a correct init sequence;
   Samsung's `ss_dsi_panel_S6E3FAC_AMB606AW01.c` showed the missing step: **DCS 0x53 (WRCTRLD, brightness control on) before 0x51**.
2. **Panel driver from the vendor DT with `linux-mdss-dsi-panel-driver-generator`**, then fix by hand: command mode (no `MIPI_DSI_MODE_VIDEO`),
   PPS sent once from `drm_dsc_config` (2 slices 540x117, DSC 1.1, 8 bpc/bpp), display on/off from the stock `display_on/off_tx_cmds`,
   regulators/GPIOs from the stock DSI node (`kernel/panel-samsung-s6e3fac.c`, DT in `kernel/sm8450-samsung-r0q.dts`).
   `dispcc_sm8450` must be a module (built-in garbled the simple-framebuffer).
3. **UFS on this phone has 4K sectors**: the pmOS rootfs image must be built with `rootfs_image_sector_size=4096`.
4. **uniLoader as secondary loader** with Image + DTB + ramdisk embedded, in the kernel slot of a v4 boot image; `CONFIG_CMDLINE_FORCE`.
   The bootloader's DTB memory node had size 0 -> give explicit RAM ranges and Samsung no-map carve-outs.
5. **`qcrypto` hangs the phone** (Samsung TZ) - keep it blocklisted. RTC is read-only (firmware owned): save/restore time yourself.
6. **MAX77705 rev 0x2** needs the upstream rev2 patch; the **fuel gauge will not register while `power-supplies = <&charger>`
   points at a charger driver that is not loaded**; the charger is an I2C driver at 0x69 (it works once loaded).
7. **Touch**: upstream FTS2BA61Y v1 patches + spi-gpio (hardware SPI 8 does not work); `usleep(1)` -> `fsleep(1)`.
8. **Never unbind `msm-mdss` live** (oopsed and hung the phone). Install display modules and reboot.
9. **Bluetooth** needs the antenna-switch rail (PMR735A L7, 2.8 V always-on) or discovery finds nothing.
10. Safe flash loop without Download-mode buttons: TWRP in the recovery slot, `adb push boot.img`, sha256 check, `dd` to `boot`, read back, `adb reboot`.

## Wi-Fi: works (5 GHz)

Chip: PCI `17cb:1103`, QCNFA765, ath11k "wcn6855 hw2.1" (Samsung: QCA6490). Four things were needed:
1. **pcie-qcom BDF->SID fix** (mainline bug, 5-cell `iommu-map`): the chip's MSIs were lost. Patch in `kernel/upstream-patches/`.
2. **Galaxy Tab S8+ DT values** (S2E/S1C, no IOVA pool, mainline PHY): fixed the firmware bootloader crash.
3. **Samsung's own firmware + board file + regdb** (`tools/s22-wifi-firmware-setup.sh`). The generic board file leaves the RF front
   end unconfigured (-74 dBm at 6 m, TX never heard). Trap: an *empty* override `board-2.bin` is ignored by the firmware loader,
   so the generic one silently wins - use a small invalid file so ath11k falls back to `board.bin`.
4. **Cold boot calibration** (stock DT `qcom,wlan-cbc-enabled`): ath11k patch enabling `coldboot_cal_mm` + `cbcal_restart_fw` for
   WCN6855 hw2.1. Without it every transmitted frame came back "no ACK".

Result: "HOME-5G" 5240 MHz, -59 dBm, TX 960 Mbit/s HE-MCS 9 2x2. 2.4 GHz association still times out - next.

## Layout

- `phone-config/` - phone-side files (blocklist, clock service, USB internet script, tinydm autologin)
- `docs/` - SETUP, flashing workflow, boot log (every attempt, error and fix), research notes, boot image notes, checklist
- `logs/` - raw phone logs and module tests (`aNN-*` = attempt NN)
- `kernel/` - config + fragment, device tree (latest), panel driver, debug patch, upstream patches used (Eric Goncalves' r0q v2 series,
  FTS2BA61Y v1, MAX77705 rev2) - patches keep their original authorship/licence (GPL-2.0)
- `tools/` - `setup-host.sh` (one-time PC dependencies), `build-uniloader.sh`, `pack-boot-img.py` (boot image v4 packer), `reboot2.c` (reboot with a reason string), display test script, lmdpdg patch (PPS + timing selection)
- `wifi-debug/` - BHI register watcher, regulator test module

Note: uniLoader's own AGENTS.md forbids LLM-generated changes inside its tree; `build-uniloader.sh` builds an unmodified copy.
Developed with the help of an AI assistant (Claude Code). Use at your own risk: flashing can brick your phone; never flash Android 16 /
One UI 8 firmware (relocks the bootloader) and never touch efs/sec_efs/persist/modem partitions.
