# Changelog - from stock Android to a working mainline Linux phone (2026-10-17 to 2026-10-18)

Everything below went from zero to working in about two days on a phone with no mainline support for the board's panel,
and with a locked-down Samsung firmware in the way. Details for every step: [boot-log.md](boot-log.md).
Wi-Fi is the one big item still open. Even the failed Wi-Fi attempts are useful: each one removes a suspect (see README).

## Milestones

| Date | Attempt | What changed | Result |
|---|---|---|---|
| 10-17 | 1-2 | uniLoader as secondary loader in the `boot` slot, explicit RAM ranges + Samsung no-map carve-outs in the DT, ramoops/console debug | kernel runs |
| 10-17 | 3, 3b | `/dev/console` nodes baked into the initramfs, real pmOS initramfs | **Linux boots**, initramfs debug shell |
| 10-17 | 4, 4b | USB gadget networking, diagnostic fixes | **root shell over USB** |
| 10-17 | 5 | reboot reasons / keys (`reboot2`) | **button-free reboot to TWRP** (the flash loop) |
| 10-17 | 6-7 | UFS, clock controllers, PON | **UFS storage works** |
| 10-17 | 8, 8b | real pmOS init, rootfs image rebuilt with **4K sectors** | **postmarketOS boots from internal storage, SSH works** |
| 10-17 | 9 | fixed USB MAC (stable `enx022252000002` name) | stable PC-side networking |
| 10-17 | - | internet sharing over USB, NTP | phone online through the PC |
| 10-17 | 10 | module bundle: PCIe PHY, pwrseq, GENI I2C, hci_uart, MAX77705 (rev 0x2 patch), zram | **Bluetooth works**, battery readable, zram swap |
| 10-17 | 11 | FTS2BA61Y touch (spi-gpio), antenna-switch rail, CDSP memory split, battery limits | **touch works (multitouch)**, BT scanning works |
| 10-17 | - | Phosh installed (stevia schemas, seat retrigger, glycin) | desktop UI runs |
| 10-17 | 12 | Adreno 730 render-only with Samsung's signed zap shader | **GPU works** (freedreno FD730, GL 4.6) |
| 10-17 | 13 | new **S6E3FAC panel driver** + MDSS/DSI0 DT, Wi-Fi rails to stock voltages | panel probes, picture still black |
| 10-18 | 13 (fix) | Samsung OEM source: send **DCS 0x53** (brightness control) before 0x51 | **real display works, much faster than simple-framebuffer** |
| 10-18 | 14 | `dma-coherent` on pcie0 | Wi-Fi unchanged |
| 10-18 | 15 | fuel gauge no longer waits for the charger | **battery percentage works** |
| 10-18 | - | charger driver unblocked (live), tinydm autologin, Phosh sessions hidden | **charging works (99% -> 100% seen)**, **Plasma Mobile only** |
| 10-18 | 16 | Wi-Fi IOVA window (stock `0xa0000000`+256 MiB), gauge linked to charger | reboot test passed, Wi-Fi unchanged |
| 10-18 | 17 | pcie-qcom BDF->SID fix (iommu-map parsed with #iommu-cells=2) | **Wi-Fi interrupts work, firmware upload OK**; SBL stage crashes (open) |
| 10-18 | 18 | pcie0 without dma-coherent (like mainline) | Wi-Fi unchanged (SBL SYS_ERR) |
| 10-18 | 19 | match Galaxy Tab S8+ (S2E/S1C mins, no IOVA pool, mainline PHY, upstream fw) | **Wi-Fi WORKS** (2.4 + 5 GHz scan) |
| 10-18 | 20 | modem: remoteproc_mpss + Samsung modem fw, rmtfs from EFS copies (read-only) | **modem boots** (QMI online, 52 services); no SIM to test |
| 10-18 | 21 | drop mainline mte_mem (512 MiB, not reserved by stock) | **+511 MiB usable RAM** (6.6 GiB) |
| 10-18 | 22 | nftables kernel (#19), hwclock dropped | **firewall up**, boot errors gone |
| 10-18 | 23 | MAX77705 Type-C (Tab S8+ patches), usb_1 otg + role switch | **USB connects at boot without replug** |
| 10-18 | 24 | ADSP + 2x CS35L41 + sound card, 2 kernel fixes (CS35L41 PLL ref, MI2S BCLK at prepare), UCM | **speaker audio works** |
| 10-18 | 25 | CS35L41 right-channel-amp (Samsung DT property) | **stereo speakers** |
| 10-18 | 26 | VA macro + DMICs (LPI 6/7, 8/9, 12/13), L12C | **microphones work** |
| 10-18 | 27 | gpio-keys: volume up on pm8350 gpio6 | **volume up works** |
| 10-18 | - | Samsung Wi-Fi fw + board file (really loaded) + regdb + ath11k cold boot calibration | **Wi-Fi CONNECTS (5 GHz, 960 Mbit/s link)** |

## Added to the repo

- `kernel/panel-samsung-s6e3fac.c` - new mainline-style DSI command-mode panel driver (DSC, backlight, Samsung init sequence)
- `kernel/sm8450-samsung-r0q.dts` - device tree: panel/DSI/MDSS, GPU, PCIe + Wi-Fi power sequencer, touch, battery/charger, regulators
- `kernel/upstream-patches/` - FTS2BA61Y touch driver, MAX77705 rev 0x2, upstream r0q DT series
- `tools/` - uniLoader build wrapper, boot image packer, `reboot2`, display test, lmdpdg patch
- `docs/flashing-workflow.md` - the TWRP + adb flash/reboot loop

## Fixed along the way (each cost real time)

- Hang after "Booting Kernel": DTB memory node had size 0
- No `/dev/console` -> init panic; pmOS initramfs overrode our canary
- USB needs a physical replug (no VBUS detection); fixed MAC so the interface name is stable
- pmOS could not find the rootfs: UFS uses 4K sectors
- `qcrypto` autoload hung the whole phone (blocklisted for good)
- Built-in `dispcc` garbled the framebuffer: must be a module
- RTC is read-only: time saved/restored by a small init service, NTP does the rest
- Bluetooth found nothing until the antenna-switch rail (PMR735A L7) was always-on
- Fuel gauge never registered while it depended on an unloaded charger driver
- Display black despite a correct init sequence: missing DCS 0x53 (found in Samsung's own S901E source)
- Hot-unbinding `msm-mdss` oopsed the phone: install modules and reboot instead

## Still open

Wi-Fi (details in README), audio, cellular modem, sleep/suspend, sensors, cameras, NFC, cable detection. See
[CHECKLIST-not-sorted.md](CHECKLIST-not-sorted.md).
