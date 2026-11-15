# S22 (SM-S901E, r0q) mainline Linux: what fixed what

From flashing uniLoader up to attempt 39 (2026-10-20). Kernel 7.2.11, postmarketOS Plasma Mobile.

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

| 35 | No NPU: CDSP memory clobbered by the DTB left at the end of the uniLoader image | uniLoader DTB relocation to 0xb6000000 (attempt 29), full 32 MiB cdsp_mem, &remoteproc_cdsp | Tab S8+ (patch) + Samsung file (cdsp.mdt) |
| 36 | NPU can't run code | quic/fastrpc built, Samsung DSP libs from the dsp partition, system_heap, cdsprpcd service, udev; unsigned PD on the normal (not secure) channel | Ours + Samsung file |
| 37 | Memory bus pinned at max forever (qcrypto blocked, interconnect sync_state never ran) | CPU + DDR bwmon nodes and GPU gfx-mem votes from stock tables, then &crypto disabled (attempts 31-32) | Samsung kernel (stock tables) + Ours |
| 38 | Vulkan missing | mesa-vulkan-freedreno (Turnip) | Upstream (Mesa) |
| 39 | Brightness slider went fully black at the bottom | Panel driver maps the slider onto DBV 6-2047 | Ours |
| 40 | No vibration | CS40L26 on i2c9 (QUP1 enabled, reset held), out-of-tree ROM-effect FF driver, feedbackd udev rule (attempts 33-34) | Samsung kernel (wiring) + Ours |
| 41 | No camera subsystem | SM8450 CAMSS + camcc fixes, &camss/&cci0/&cci1 (attempt 35) | Tab S8+ |
| 42 | Camera rails unreachable | S2MPB02 camera PMIC regulator driver on i2c-gpio 175/176 (attempt 36) | Samsung kernel (GPL driver, wiring) + Ours |
| 43 | No sensor settings | Vendor partition read via F2FS (with compression) module; CamX Chromatix decoder (tools/camx-chromatix-dump.py) | Ours |
| 44 | 2.4 GHz Wi-Fi TX dead (`NO_ACK`, auth/association timed out) | `s22-wlan-pdc` enables QCA6490 2.2 V internal 2.4 GHz PA rail (`s3e.e upval: 1`) via AOSS QMP before cold boot calibration | Samsung kernel (`cnss2`/`btpower` QMP) + Ours (Gemini 4 Carbon) |
| 45 | Read-only RTC (`1970` at boot before NTP) | `&pmk8350_rtc` wired to `&pmk8350_sdam_21` `rtc-offset@78` (128-byte HLOS-writable SPMI SDAM); `rtc_pm8xxx` unblocked | Mainline `rtc-pm8xxx` + Ours (Gemini 4 Carbon) |
| 46 | No sensors (auto-rotate, ambient light, proximity, compass) | `&remoteproc_slpi` (`slpi.mdt`) + stock `L2C` (1.8 V) / `L9C` (3.0 V) rails + `s22-hex-cb3` (`0x0543` `SENSORS_PD` CB) + `hexagonrpcd` + `iio-sensor-proxy` | Stock DTBO (`fragment@47`/`143`) + Ours (Gemini 4 Carbon) |
| 47 | Deep suspend/resume USB check | Verified `deep` sleep + RTC alarm / `pmic_pwrkey` wakeup with `max77705_typec` + `dwc3-host-power-lost`: USB NCM & Wi-Fi survive resume without replug | Tab S8+ + Ours (Gemini 4 Carbon) |
| 48 | USB 2.0 peripheral only (no USB 3.0 SuperSpeed) | Enable `&usb_1_qmpphy` (`vreg_l1b_0p91` + `vreg_l6b_1p2`), add 4nm SM8450/r0q QMP RX & PCS lock-detect tuning (`0xc4`/`0x89`, stock `fragment@152`), fix `dwc3-qcom` role VBUS/`LANE0_PWR_PRESENT` override, set `maximum-speed = "super-speed"` (`fragment@150`), build USB3/Type-C stack in | Samsung kernel (DTBO values) + Ours (Gemini 4 Carbon) |
| 49 | No DisplayPort over USB-C | Enable `&mdss_dp0` (`card1-DP-1`, 2 lanes up to HBR3), add `gpio-sbu-mux` (`gpio125` `/OE` active-low, `gpio108` `aux_sel`), wire OF-graph ports, and implement SVDM `0xff01` (Discover Identity/SVIDs/Modes, Enter Mode, DP Status, DP Configure Pin C/D/E, Attention/HPD + `drm_dp_hpd_bridge`) in `max77705-usbc` | Samsung kernel (`max77705_alternate.c`, `secdp`) + Ours (Gemini 4 Carbon) |
| 50 | 342 MiB of HLOS RAM still locked in Android-only `no-map` regions | Drop DTBO-disabled `oem_vm_mem` (80M) & `global_sync_mem` (1M), Android-only `trust_ui_vm_*` (76M), boot-freed `samsung-rdx-bootdev` (173M) & `samsung-sec-debug` (10M), and shrink `slpi_mem` to stock 23M (`fragment@123`), keeping modem/camera/CVP intact -> **7,161 MiB (7.0 GiB) usable** | Samsung kernel (`sec_qc_rdx_bootdev.c`, stock DTBO) + Ours (Gemini 4 Carbon) |

| 51 | Modem not listed by ModemManager, no mobile data (no SM8450 IPA), manual start only | Backport SM8450 IPA v5.1 (`qcom,sm8450-ipa`, `ipa_data-v5.1.c`), wire `&ipa` to stock `ipa_gsi_mem` (`0x8b910000` PAS 15 `apnhlos` `ipa_fws.mdt`), enable `RMNET` + `RPMSG_WWAN_CTRL`, add `s22-modem.service` (auto-boot + Dual-SIM QMI UIM provisioning + GNSS), read-only `rmtfs` drop-in, `81voltd`, and `VoiceCall`/`Earpiece` UCM2 | Upstream LKML (E. Urrutia / N. Kompanets) + Samsung kernel (`ipa.c`) + Ours (Gemini 4 Carbon) |
| 52 | Fixed 120 Hz only (no dynamic VRR), no HBM, no colour calibration, no Always-On Display | Upgrade `panel-samsung-s6e3fac` with 6 stock FHD modes (120/96/60/48/30/24 Hz) + glitch-free seamless switching + hardware PHS/LFD adaptive VRR (10-120 Hz), 12-bit HBM (`0x0800..0x0fff`, ~1300 nits + `STK33915` auto-lux), hardware mDNIe/IRC colour modes + factory `0xA1` OTP white-point calibration + RGB/Kelvin LUT, hardware HLPM Always-On Display (2/10/30/60 nit + pocket guard), and `s22-display-ctl` | Samsung kernel (`S6E3FAC_AMB606AW01`) + Ours (Gemini 4 Carbon) |

| 53 | Display helpers silently skipped `kscreen-doctor` when the compositor socket was `wayland-1` instead of `wayland-0` | Discover `wayland-[0-9]` in `s22-display-ctl` / `s22-display-extrasd`, and resolve the session user from `/etc/passwd` (busybox `id -nu` fails under sudo) before calling `kscreen-doctor` | Ours |
| 54 | KWin composited on `llvmpipe` because the Plasma session started before the Adreno DRM nodes existed | `startplasmamobile` now drops `EGL_PLATFORM=wayland` and waits for `/dev/dri/card0` + `/dev/dri/renderD128` before launching KWin; KWin selects `renderD128` / `freedreno FD730` | Ours |
| 55 | Front camera (`IMX374`) CCI1 bus SDA/SCL held low (I2C timeouts) & missing rear camera rails/drivers | Expand `s2mpb02-regulator` to all 21 regulators (`LDO1-18`, `BUCK1`, `BUCK2` 1.3 V, `BUCK_BOOST` 3.3 V) and keep shared I/O & sub-PMIC rails (`buck2`, `bb`, `ldo6`, `ldo7`, `ldo9`) always-on per stock DTBO (`s2mpb02_pmic@59`), releasing the CCI pull-ups; enable `&gpi_dma2` + `&i2c16` (1 MHz) + `gpio83` (`CAM_WIDE_LONG_EXPOSURE` low) and add V4L2 drivers for `imx374`, `s5k3k1`, `imx563`, and `s5kgn3` | Stock DTBO (`fragment@68`/`104`) + CamX sensormodules + Ours |
| 56 | Ultrawide (`IMX563`) & Main Wide (`S5KGN3`) use 3-trio MIPI C-PHY (rejected by mainline SM8450 CAMSS) | Add 3-phase C-PHY support (`MEDIA_BUS_TYPE_CSI2_CPHY`, `CSI2_RX_CFG0_PHY_TYPE_SEL = 1`, SM8450 5nm 3PH Trio0/1/2 lane config, 1.50-3.30 GSpS rate delta table, and 400 MHz `csiphy_timer` frequency clamp) in `local-sm8450-camss-cphy.patch` | Downstream `csiphy_v2_0_1_3ph` + Ours |

## Not fixed yet
- Speaker protection firmware (volume kept moderate)
- Keyboard auto-hide
- Unused clocks/power domains kept on (clk_ignore_unused); removing them broke the display probe (attempt 40, reverted)
- NPU compute runtime (needs Qualcomm SDK); NPU first-call race
- Fingerprint (QBT2000/QBT2608 TrustZone-only), NFC

## Rules kept throughout
- Never write the EFS partitions (efs, sec_efs, persist, modemst1/2, fsg, fsc)
- Never flash Android 16 / One UI 8 or a newer bootloader
- Keep `qcrypto` blocked; never unbind msm-mdss live; always give `reboot2` an argument
- Never `rmmod` ath11k while calibration is stuck
