# S22 Checklist (Linux Install)
tags: #s22 #checklist
Updated 2026-11-11 ? kernel 7.2.11

## ✅ Working
- [x] Boots postmarketOS from internal storage (~35 s)
- [x] USB 3.0 SuperSpeed (5 Gbps, CC1/CC2 orientation) + USB 2.0 fallback + SSH at boot, no replug ([[USB-C]])
- [x] USB plug/unplug detection (attempt 28)
- [x] MTP file transfer to PC
- [x] Wi-Fi 2.4 GHz + 5 GHz, autoconnects at boot ([[Wi-Fi]])
- [x] Bluetooth
- [x] Bluetooth audio in (A2DP sink): phone plays music streamed from another device (2026-10-21)
- [x] Bluetooth audio out: connects to a car stereo, clear audio (2026-10-21)
- [x] Display 120/96/60/48/30/24 Hz + **Dynamic VRR (10-120 Hz PHS/LFD & seamless switch), HBM (~1300 nits + auto-lux), hardware mDNIe/IRC/OTP colour calibration & Always-On Display (HLPM 2/10/30/60 nit)**, touch, GPU ([[Display]]); CPU frequency scaling + PSCI idle states verified
- [x] **KWin on Adreno 730**: session waits for `/dev/dri/card0` + `renderD128` before starting; `freedreno/FD730`, no `llvmpipe` (2026-11-12)
- [x] **Wayland socket discovery** (`wayland-[0-9]`) in `s22-display-ctl` / `s22-display-extrasd`; 120↔60 switching works through the helper (2026-11-12)
- [x] Stereo speakers + handset **Earpiece** + 2 mics + **VoiceCall UCM2** ([[Audio]]); amp digital volume +3 dB (841), no speaker protection yet
- [x] Power, volume down, volume up buttons
- [x] On-screen keyboard
- [x] Charging + battery %
- [x] RAM 7.0 GiB (7,161 MiB) usable (+342 MiB reclaimed from Android-only carve-outs; modem/camera/CVP regions kept)
- [x] Firewall (nftables)
- [x] Apps: Google Chrome, QMLKonsole, Discover + Flathub, KRecorder, Dolphin ([[Software]])
- [x] USB OTG (FAT/exFAT/ISO) + USB-PD (5V/9V) + DisplayPort Alt-Mode out (`card1-DP-1`, SVDM `0xff01` Pin C/D/E, up to HBR3)
- [x] **CDSP (NPU processor) running**, /dev/fastrpc-cdsp (attempt 29, uniLoader DTB relocation)
- [x] NPU: FastRPC calculator test passes on the CDSP, unsigned PD (~4/6; normal channel + cdsprpcd service, survives reboot)
- [x] **Vulkan (Turnip) on Adreno 730**
- [x] **Camera subsystem (CAMSS)**: 6 CSIPHY, 5 CSID, 5 VFE, 17 video nodes, 4 CCI buses (Tab S8+ patches, attempt 35)
- [x] **Camera power PMIC (S2MPB02)**: 21 regulators (LDO1-18, BUCK1, BUCK2 1.3 V, BUCK_BOOST 3.3 V) with stock shared I/O & sub-PMIC supply rails enabled (attempt 36 + full bring-up)
- [x] **All 4 cameras (front + triple rear, CAMSS D-PHY & 3-phase C-PHY RAW10)**: front **Sony IMX374** (`cci1_i2c1 @ 0x10`, `CSIPHY4` 4-lane D-PHY, `1616x912` & `3648x2736`), 3x tele **Samsung S5K3K1** (`cci0_i2c1 @ 0x2d`, `CSIPHY0` 4-lane D-PHY, `3648x2736` & `1824x1368`), ultrawide **Sony IMX563** (`cci0_i2c0 @ 0x10`, `CSIPHY2` 3-trio C-PHY, `4032x3024` & `2016x1512`), and 50MP main wide **Samsung S5KGN3** (`i2c16 @ 0x10` + `gpi_dma2`, `CSIPHY1` 3-trio C-PHY, `4080x3060` & `2040x1532`)
- [x] **Vibration**: CS40L26 driver (ROM effects, medium strength), works with Plasma/feedbackd (attempt 34)
- [x] Brightness slider, with a minimum (DBV 6) so it never goes fully black
- [x] **Memory bandwidth voting**: CPU + DDR bwmon and GPU votes from stock tables; memory bus idles at its lowest step instead of max (attempts 31-32)
- [x] **Idle battery**: ~3.2 %/h (~31 h projected), was ~8 h before the memory bus fix (overnight test, 2026-10-21)
- [x] **Sensors**: SLPI sensor hub (`remoteproc_slpi` + stock `L2C` 1.8 V / `L9C` 3.0 V rails) + `hexagonrpcd-sdsp` (`s22-hex-cb3` on CB3 `0x0543`) + `iio-sensor-proxy`: accelerometer/auto-rotate (`LSM6DSO`), ambient light/auto-brightness (`STK33915`), proximity (`STK33915`), compass (`AK09918`)
- [x] **Writable hardware RTC**: `pmk8350_rtc` wired to battery-backed `pmk8350_sdam_21` (`rtc-offset@78`), `/dev/rtc0` writable (`hwclock --systohc`, `chrony rtcsync`)
- [x] **Suspend/resume**: `deep` sleep + RTC/power-button wake verified; Wi-Fi, touch, sensors and **USB NCM survive resume without replug**

## 🟡 Partly working
- [x] **Cellular modem + SM8450 IPA v5.1 (`qcom,sm8450-ipa`)**: auto-starts at boot (`s22-modem.service` + read-only `rmtfs`), exposes `rmnet_ipa0` + `qrtr0` + `wwan0at0` to **ModemManager (`qcom-soc`)**, auto-provisions Dual-SIM USIM, VoLTE (`81voltd`), VoiceCall UCM2 & GNSS (SIM live test ready) ([[Modem]])
- [ ] Keyboard auto-hide
- [ ] Android phones can't browse MTP (PC works)
- [x] OTG: ISO9660, UDF and exFAT enabled as kernel modules
- [ ] NPU userspace (FastRPC library) - CDSP is up
- [ ] NPU first-call race (skel load fails ~1 in 3; apps should retry)
- [ ] Unused clocks/power domains stay on (clk_ignore_unused pd_ignore_unused). Attempt 40 removed them: works but the display probe timed out (reverted). Next: longer deferred_probe_timeout

## ❌ Not done
- [x] **Mobile data (`sm8450-ipa` v5.1 + `rmnet` enabled)** & **Calls / SMS stack** (`ModemManager` + `rpmsg_wwan_ctrl` + `81voltd` + `VoiceCall` UCM2 configured; ready for physical SIM test)
- [ ] Speaker protection firmware (louder volume)
- [ ] Vibration: full CS40L26 driver with Samsung firmware + calibration (stronger, custom waveforms)
- [ ] Fingerprint (Qualcomm 3D Sonic QBT2000, TrustZone-only: unlikely)
- [ ] NFC

## 📋 Housekeeping
- [ ] Clean debug leftovers (ath11k debug build, `~/s22-tests`)
- [ ] Battery charge limit control
- [x] Portrait panel orientation (`DRM_MODE_PANEL_ORIENTATION_NORMAL`) reported by S6E3FAC driver
- [ ] Upstream pcie-qcom patch (skipped for now)
