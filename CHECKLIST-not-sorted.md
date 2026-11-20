# S22 (SM-S901E) Linux - what's sorted and what's not (2026-10-18, updated 10:50)

## Working
- [x] Boots postmarketOS from internal storage (uniLoader + mainline 7.2.11)
- [x] USB networking + SSH (PC internet sharing)
- [x] Display: real S6E3FAC panel driver, frames straight to panel
- [x] Touch (multitouch, orientation correct)
- [x] GPU (Adreno 730 freedreno, GPU compositing)
- [x] Bluetooth (address from EFS, scans devices)
- [x] zram swap, thermal sensors, NTP clock
- [x] Plasma Mobile only (tinydm autologin); power, volume down and (since attempt 27) VOLUME UP work; clock saved/restored across boots

## Not sorted - needs work
- [x] **Wi-Fi CONNECTS** (2.4 GHz + 5 GHz, Wi-Fi 6 2x2, 960 Mbit/s link), autoconnects at boot (ath11k loaded late by /etc/local.d/s22-wifi.start; 2.4 GHz internal PA rail `s3e` enabled via AOSS QMP by `s22-wlan-pdc`)
- [x] **Wi-Fi signal** fixed by Samsung board file (-35 dBm 2.4 GHz, -51 dBm 5 GHz at 6 m)
- [x] **RAM**: +854 MiB total reclaimed (dropped mainline `mte_mem`, disabled `oem_vm_mem`/`global_sync_mem`, Android-only `trust_ui_vm_*`/`rdx_bootdev`/`sec_debug`, stock 23 MiB `slpi_mem`; modem/camera/CVP kept), **7.0 GiB (7,161 MiB) usable**
- [x] **On-screen keyboard** (plasma-keyboard as KWin input method; hide key works, no auto-hide yet)
- [x] **Charging**: works (charger driver unblocked, battery seen going 99% -> 100%); battery status icon may say Unknown until DT links gauge to charger
- [x] **Battery %**: works (max170xx_battery, DT dependency on charger removed)
- [x] **USB auto-connect at boot** (MAX77705 Type-C role switch, attempt 23; SSH ~25 s after boot, no replug)
- [x] **Firewall** (nftables, kernel #19)
- [x] **Apps**: QMLKonsole terminal, Google Chrome (Flathub, official ARM64), Chromium, Discover + Flathub app store, KRecorder (GStreamer backend)
- [x] **Volume up** button (gpio-keys, attempt 27)
- [x] **Audio**: stereo loudspeakers (top = left, bottom = right) + 2 microphones, PulseAudio Speaker sink + Mic source (attempts 24-26). Still: speaker protection fw, call audio
- [x] **Cellular modem + SM8450 IPA v5.1 (`qcom,sm8450-ipa`)**: auto-starts at boot (`s22-modem.service`, read-only `rmtfs` in `/var/lib/rmtfs`), IPA v5.1 (`ipa_gsi_mem` PAS 15 `ipa_fws.mdt`) + `rmnet` + `rpmsg_wwan_ctrl` enumerate in **ModemManager (`qcom-soc`)** for **4G/5G mobile data, SMS, Voice Calls (`81voltd` + `VoiceCall` UCM2 + `Earpiece`), Dual-SIM QMI UIM auto-provisioning & GNSS** (physical SIM test ready)
- [x] **Sleep / suspend / resume**: `deep` sleep + RTC/power-button wake verified; Wi-Fi, touch, sensors and USB NCM survive resume without replug
- [x] **Brightness control in the UI** (slider mapped to DBV 6-2047) + **auto-brightness** via `STK33915` ambient light sensor (`iio-sensor-proxy`)
- [x] **Sensors**: SLPI sensor hub (`remoteproc_slpi` + `L2C`/`L9C` rails + `s22-hex-cb3` + `hexagonrpcd`) -> auto-rotate (`LSM6DSO`), proximity & ambient light (`STK33915`), compass (`AK09918`)
- [x] **Vibration / haptics**: Cirrus CS40L26 (`cs40l26-rom-vibra` on `i2c9 @ 0x40` + `feedbackd`)
- [ ] **Cameras**, flash
- [ ] **Fingerprint** reader
- [ ] **NFC**
- [x] **USB-C**: host/OTG (with VBUS, FAT/exFAT/ISO), USB-PD (5V/3A & 9V/2A), **USB 3.0 SuperSpeed (5 Gbps, CC1/CC2 orientation)**, and **DisplayPort Alt-Mode (`card1-DP-1`, SVDM `0xff01` Pin C/D/E)**
- [x] **Writable hardware RTC**: `pmk8350_rtc` wired to `pmk8350_sdam_21` (`rtc-offset@78`); `/dev/rtc0` persists wall-clock time across reboots before network
- [x] **120 Hz** (panel driver refresh=120 default, stock fhd120hs timing + commands)
- [x] **Display extras (`panel-samsung-s6e3fac` + `s22-display-ctl` + `s22-display-extras.service`)**: **VRR** (hardware 10-120 Hz / 24-120 Hz PHS/LFD + seamless non-blanking 120/96/60/48/30/24 Hz DRM modes), **HBM** (12-bit DBV up to 4095 / ~1300 nits + `STK33915` sunlight auto-boost), **colour calibration** (hardware mDNIe + IRC Vivid/Natural sRGB/Standard/Auto/Bypass/Grayscale/Night 2300-6500K/Outdoor + factory `0xA1` OTP CIE white-point calibration + RGB white balance + DPU PCC CTM), and **Always-On Display** (hardware HLPM 2/10/30/60 nit self-refresh + proximity pocket guard)

## Known rough edges / tech debt
- [ ] `panel = <&panel_size>` on simple-framebuffer blocks it from probing (no fallback display if the DSI path fails); fix needs care with card numbering (WLR_DRM_DEVICES=/dev/dri/card1 is hardcoded)
- [ ] Never unbind msm-mdss live (oopsed once); update display drivers by module install + reboot
- [ ] CDSP memory shrunk (2 MiB + 15 parts) - restore a full 32 MiB before any DSP work
- [ ] dispcc built as module; display relies on it loading at boot (blocklist line commented out)
- [x] Plasma Mobile: UI scale (2.5x), nav bar (Back/Home/Overview), KWin GPU rendering set up on Adreno 730 (unified msm DRM card0 + Always-On Display & Game 90Hz QuickSettings)
- [ ] Phosh: on-screen keyboard popping up by itself; home bar should now appear on DSI-1 (unverified)
- [ ] Debug leftovers: mhi/host/Makefile still has -DDEBUG (tree), test modules in ~ on the phone (mhi-poll, ath11k_pci-*, rfclk), panel driver readback prints, ramoops/console debug patch in the kernel
- [ ] Upstream-pending patches carried locally: FTS2BA61Y touch driver, MAX77705 rev 0x2, r0q device tree, new panel driver
- [ ] Kernel cmdline is forced; module blocklist /etc/modprobe.d/s22-bringup.conf still lists several modules (qcrypto must stay blocked: it hangs the phone)
- [ ] Reboot helper: `~/reboot2` with no argument reboots into the last target (went to TWRP once) - always pass an explicit argument
- [-] (skipped by user) Upstream patch ready to send: kernel-config/upstream-patches/0001-PCI-qcom-Parse-iommu-map-entries-using-iommu-cells.patch (Fixes filled; needs your name + Signed-off-by)
- [x] Portrait panel orientation (`DRM_MODE_PANEL_ORIENTATION_NORMAL`) wired in `panel-samsung-s6e3fac`
- [ ] NOTE for new ports / reinstalls: install USB-C auto-detection (MAX77705 Type-C driver + DT role switch, kernel-config/upstream-patches/tabs8/) ASAP, right after USB networking works - it removes the replug on every test boot
- [x] USB link drops = system suspend (dwc3 HS-PHY not in L2 on resume); suspend blocked for now (elogind sleep.conf.d + powerdevilrc)
- [x] **USB plug/unplug detection** (attempt 28): MAX77705 IRQ pin gpio5 power-source 0 -> 1 (stock if_pmic_irq). Was: 200001-IRQ storm then dead, no hotplug
- [x] **MTP file transfer to PC** (umtprd + FunctionFS next to NCM, /usr/local/sbin/s22-mtp-gadget, /etc/local.d/s22-mtp.start). Android hosts can't browse MTP devices
- [x] Chromium removed; Google Chrome launcher in ~/.local/share/applications; Plasma env sources flatpak.sh
- [x] PC "S22 USB (pmOS)" NM profile no longer ipv4 shared
