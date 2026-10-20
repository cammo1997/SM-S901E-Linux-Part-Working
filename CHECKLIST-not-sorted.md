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
- [x] **Wi-Fi CONNECTS** (5 GHz, Wi-Fi 6 2x2, 960 Mbit/s link), autoconnects at boot (ath11k loaded late by /etc/local.d/s22-wifi.start). 2.4 GHz connect still to fix
- [x] **Wi-Fi signal** fixed by Samsung board file (-35 dBm 2.4 GHz, -51 dBm 5 GHz at 6 m)
- [x] **RAM**: +512 MiB (dropped mainline mte_mem), 6.6 GiB usable (attempt 21)
- [x] **On-screen keyboard** (plasma-keyboard as KWin input method; hide key works, no auto-hide yet)
- [x] **Charging**: works (charger driver unblocked, battery seen going 99% -> 100%); battery status icon may say Unknown until DT links gauge to charger
- [x] **Battery %**: works (max170xx_battery, DT dependency on charger removed)
- [x] **USB auto-connect at boot** (MAX77705 Type-C role switch, attempt 23; SSH ~25 s after boot, no replug)
- [x] **Firewall** (nftables, kernel #19)
- [x] **Apps**: QMLKonsole terminal, Google Chrome (Flathub, official ARM64), Chromium, Discover + Flathub app store, KRecorder (GStreamer backend)
- [x] **Volume up** button (gpio-keys, attempt 27)
- [x] **Audio**: stereo loudspeakers (top = left, bottom = right) + 2 microphones, PulseAudio Speaker sink + Mic source (attempts 24-26). Still: speaker protection fw, call audio
- [~] **Cellular modem**: boots (attempt 20), QMI online, 52 services; rmtfs from EFS copies read-only. Started by hand (not at boot).
      No SIM inserted yet; ModemManager does not list it yet. SMS/calls untested; mobile data needs sm8450 IPA (not in mainline)
- [ ] **Sleep / suspend / resume**, screen off + wake, power button behaviour (untested)
- [ ] **Brightness control in the UI** (backlight exists; slider untested), auto-brightness
- [ ] **Sensors**: auto-rotate (accelerometer/gyro), proximity, ambient light
- [ ] **Vibration / haptics** (max77705 haptic not bound)
- [ ] **Cameras**, flash
- [ ] **Fingerprint** reader
- [ ] **NFC**
- [ ] **USB-C**: host/OTG (driver + OTG VBUS in place since attempt 23, untested), PD, DisplayPort out, USB3 (currently USB2 peripheral only)
- [ ] **RTC** is read-only (clock relies on NTP; no time at boot until network)
- [x] **120 Hz** (panel driver refresh=120 default, stock fhd120hs timing + commands)
- [ ] **Display extras**: VRR (no dynamic switching), HBM, colour calibration, always-on display

## Known rough edges / tech debt
- [ ] `panel = <&panel_size>` on simple-framebuffer blocks it from probing (no fallback display if the DSI path fails); fix needs care with card numbering (WLR_DRM_DEVICES=/dev/dri/card1 is hardcoded)
- [ ] Never unbind msm-mdss live (oopsed once); update display drivers by module install + reboot
- [ ] CDSP memory shrunk (2 MiB + 15 parts) - restore a full 32 MiB before any DSP work
- [ ] dispcc built as module; display relies on it loading at boot (blocklist line commented out)
- [ ] Plasma Mobile: UI scale (2.5x), nav bar (Back/Home/Overview), KWin GPU rendering not yet set up
- [ ] Phosh: on-screen keyboard popping up by itself; home bar should now appear on DSI-1 (unverified)
- [ ] Debug leftovers: mhi/host/Makefile still has -DDEBUG (tree), test modules in ~ on the phone (mhi-poll, ath11k_pci-*, rfclk), panel driver readback prints, ramoops/console debug patch in the kernel
- [ ] Upstream-pending patches carried locally: FTS2BA61Y touch driver, MAX77705 rev 0x2, r0q device tree, new panel driver
- [ ] Kernel cmdline is forced; module blocklist /etc/modprobe.d/s22-bringup.conf still lists several modules (qcrypto must stay blocked: it hangs the phone)
- [ ] Reboot helper: `~/reboot2` with no argument reboots into the last target (went to TWRP once) - always pass an explicit argument
- [-] (skipped by user) Upstream patch ready to send: kernel-config/upstream-patches/0001-PCI-qcom-Parse-iommu-map-entries-using-iommu-cells.patch (Fixes filled; needs your name + Signed-off-by)
- [ ] Plasma Display settings label the native portrait mode "Landscape" (cosmetic; panel orientation property)
- [ ] NOTE for new ports / reinstalls: install USB-C auto-detection (MAX77705 Type-C driver + DT role switch, kernel-config/upstream-patches/tabs8/) ASAP, right after USB networking works - it removes the replug on every test boot
- [x] USB link drops = system suspend (dwc3 HS-PHY not in L2 on resume); suspend blocked for now (elogind sleep.conf.d + powerdevilrc)
- [x] **USB plug/unplug detection** (attempt 28): MAX77705 IRQ pin gpio5 power-source 0 -> 1 (stock if_pmic_irq). Was: 200001-IRQ storm then dead, no hotplug
- [x] **MTP file transfer to PC** (umtprd + FunctionFS next to NCM, /usr/local/sbin/s22-mtp-gadget, /etc/local.d/s22-mtp.start). Android hosts can't browse MTP devices
- [x] Chromium removed; Google Chrome launcher in ~/.local/share/applications; Plasma env sources flatpak.sh
- [x] PC "S22 USB (pmOS)" NM profile no longer ipv4 shared
