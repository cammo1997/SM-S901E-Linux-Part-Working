# S22 (SM-S901E, r0q) mainline Linux: what fixed what

From flashing uniLoader up to attempt 28 (2026-10-18). Kernel 7.2.11, postmarketOS Plasma Mobile.

**Source key**
- **Samsung kernel**: taken from Samsung's S901E kernel source or stock device tree (`recon/live.dts`)
- **Samsung file**: a firmware/blob/data file copied from Samsung's stock firmware or the phone
- **Tab S8+**: from the aaronsb/sm-x800-linux Galaxy Tab S8+ (SM8450) mainline port
- **Ours**: written or worked out in this project

| # | Problem | Fix | Source |
|---|---------|-----|--------|
| 1 | Samsung bootloader won't boot a mainline kernel directly | uniLoader secondary loader wrapping Image + DTB, packed with `tools/pack-boot-img.py`, flashed to `boot` (sda25) from TWRP | Ours (uniLoader is upstream) |
| 2 | No safe way back if a build fails | TWRP flash loop: `reboot2 recovery` -> `adb push` -> `dd` -> hash readback | Ours |
| 3 | Board had no mainline device tree | `sm8450-samsung-r0q.dts`, built from SM8450 mainline + stock values | Ours + Samsung kernel |
| 4 | Wi-Fi PCIe: SMMU faults, card dead | `pcie-qcom` parses `iommu-map` with `#iommu-cells` (5-cell entries) for the BDF->SID table | Ours (upstream patch written, not sent) |
| 5 | Wi-Fi rails / PCIe settings wrong | S2E min 0.5 V, S1C min 1.8 V, no `dma-coherent`, no Wi-Fi IOVA pool | Tab S8+ |
| 6 | Wi-Fi firmware crashed / no scan | Samsung `amss.bin` (WLAN.HSP.2.0.c1), `m3.bin`, `regdb.bin` in `/lib/firmware/updates/ath11k/WCN6855/hw2.1` | Samsung file |
| 7 | Wrong board data silently used | `board.bin` = Samsung `bdwlang.elf`; non-empty dummy `board-2.bin` (an empty one is ignored by the loader) | Samsung file + Ours |
| 8 | Firmware crash after calibration | ath11k hw2.1: `coldboot_cal_mm` + `cbcal_restart_fw` (stock `qcom,wlan-cbc-enabled`) | Samsung kernel (value) + Ours (patch) |
| 9 | Wi-Fi calibration timed out at boot | ath11k_pci blacklisted, loaded late by `/etc/local.d/s22-wifi.start` | Ours |
| 10 | Wrong country / channels | `cfg80211 ieee80211_regdom=AU`, `iw reg set AU` | Ours |
| 11 | Random Wi-Fi MAC | NM config + udev rule set the real MAC | Ours (MAC from Samsung EFS) |
| 12 | Modem wouldn't boot | `remoteproc_mpss` firmware `qcom/sm8450/samsung/r0q/modem.mdt` | Samsung file |
| 13 | Modem needs its EFS | rmtfs file mode, read-only, copies in `/var/lib/rmtfs` | Samsung file (EFS backup) + Ours |
| 14 | Only 6.2 GiB RAM | Memory map: adsp/video/cdsp heap moved, `samsung-adsp` and `mte_mem` removed (6.8 GiB) | Samsung kernel (stock map) + Ours |
| 15 | 60 Hz only | Panel driver `refresh=120`, fhd120hs timing (VFP 467/VPW 84/VBP 84), 120 Hz command bytes | Samsung kernel (panel values) + Ours |
| 16 | "Kernel lacks nftables" | `NF_TABLES` family enabled in `.config` | Ours |
| 17 | USB dropped at boot, needed a replug | MAX77705 Type-C/charger drivers + dwc3 patches (12), `usb_1` OTG + role switch | Tab S8+ |
| 18 | MFD/Type-C module name clash | Type-C module renamed `max77705_typec` | Ours |
| 19 | USB died after idle | Suspend blocked (elogind sleep.conf.d + powerdevil) | Ours |
| 20 | USB plug/unplug not detected (200001-IRQ storm, then dead) | MAX77705 IRQ pin gpio5 `power-source = <1>` (attempt 28) | Samsung kernel (stock `if_pmic_irq`) |
| 21 | No sound: ADSP | `remoteproc_adsp` firmware `qcom/sm8450/samsung/r0q/adsp.mdt` | Samsung file |
| 22 | Amps `Enable -110`: PLL never locked | `sc8280xp.c` also sets the codec component sysclk | Ours |
| 23 | Amps `Enable -110`: no bit clock | `q6apm-lpass-dais.c` starts MI2S at prepare | Ours |
| 24 | MI2S format/sysclk | `sc8280xp-mi2s-codec-fmt-sysclk` patch | Tab S8+ |
| 25 | Amps: addresses, resets, boost | CS35L41 @40/@41, resets 183/18, 1000 nH / 15 µF / 4100 mA | Samsung kernel |
| 26 | Both speakers playing the same channel | `cirrus,right-channel-amp` sets RX slot 1 on the top amp | Ours |
| 27 | No Speaker/Mic in Plasma | UCM2 files (Samsung-Galaxy-S22, HiFi) | Ours |
| 28 | No mics | VA macro + DMIC pins, DMIC1/DMIC4, L12C mic bias, 2.4 MHz | Samsung kernel (pins) + Ours |
| 29 | Recorder saved 0 s | KRecorder forced to the GStreamer backend | Ours |
| 30 | Volume up dead | gpio-keys on PM8350 GPIO 6 | Samsung kernel (pin) |
| 31 | No on-screen keyboard | KWin `InputMethod` = Plasma keyboard (user + `/etc/xdg`) | Ours |
| 32 | Chrome not in the app drawer | Launcher in `~/.local/share/applications`; Plasma env sources `flatpak.sh` | Ours |
| 33 | No file transfer | MTP via umtprd + FunctionFS next to NCM (`s22-mtp-gadget`) | Ours |
| 34 | PC sharing its internet to the phone | PC NM profile "S22 USB (pmOS)" set to manual, not shared | Ours |

## Not fixed yet
- 2.4 GHz Wi-Fi association times out (5 GHz works)
- Speaker protection firmware (volume kept moderate)
- Suspend/resume (blocked), keyboard auto-hide
- Modem: no SIM tested, ModemManager doesn't list it; no mobile data (no sm8450 IPA)
- Sensors, cameras, fingerprint, NFC, vibration, writable RTC

## Rules kept throughout
- Never write the EFS partitions (efs, sec_efs, persist, modemst1/2, fsg, fsc)
- Never flash Android 16 / One UI 8 or a newer bootloader
- Keep `qcrypto` blocked; never unbind msm-mdss live; always give `reboot2` an argument
- Never `rmmod` ath11k while calibration is stuck
