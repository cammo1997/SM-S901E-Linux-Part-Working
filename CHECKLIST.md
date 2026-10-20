# S22 Checklist (Linux Install)
tags: #s22 #checklist
Updated 2026-10-20 · attempt 39 · kernel 7.2.11 (#21)

## ✅ Working
- [x] Boots postmarketOS from internal storage (~35 s)
- [x] USB + SSH at boot, no replug ([[USB-C]])
- [x] USB plug/unplug detection (attempt 28)
- [x] MTP file transfer to PC
- [x] Wi-Fi 5 GHz, autoconnects at boot ([[Wi-Fi]])
- [x] Bluetooth
- [x] Bluetooth audio in (A2DP sink): phone plays music streamed from another device (2026-10-21)
- [x] Bluetooth audio out: connects to a car stereo, clear audio (2026-10-21)
- [x] Display 120 Hz, touch, GPU ([[Display]]); CPU frequency scaling + PSCI idle states verified
- [x] Stereo speakers + 2 mics ([[Audio]]); amp digital volume +3 dB (841), no speaker protection yet
- [x] Power, volume down, volume up buttons
- [x] On-screen keyboard
- [x] Charging + battery %
- [x] RAM 6.8 GiB usable
- [x] Firewall (nftables)
- [x] Apps: Google Chrome, QMLKonsole, Discover + Flathub, KRecorder, Dolphin ([[Software]])
- [x] USB OTG: USB stick detected in Dolphin (attempt 28)
- [x] **CDSP (NPU processor) running**, /dev/fastrpc-cdsp (attempt 29, uniLoader DTB relocation)
- [x] NPU: FastRPC calculator test passes on the CDSP, unsigned PD (~4/6; normal channel + cdsprpcd service, survives reboot)
- [x] **Vulkan (Turnip) on Adreno 730**
- [x] **Camera subsystem (CAMSS)**: 6 CSIPHY, 5 CSID, 5 VFE, 17 video nodes, 4 CCI buses (Tab S8+ patches, attempt 35)
- [x] **Camera power PMIC (S2MPB02)**: LDO regulator driver, front camera rails at stock voltages (attempt 36)
- [x] **Vibration**: CS40L26 driver (ROM effects, medium strength), works with Plasma/feedbackd (attempt 34)
- [x] Brightness slider, with a minimum (DBV 6) so it never goes fully black
- [x] **Memory bandwidth voting**: CPU + DDR bwmon and GPU votes from stock tables; memory bus idles at its lowest step instead of max (attempts 31-32)
- [x] **Idle battery**: ~3.2 %/h (~31 h projected), was ~8 h before the memory bus fix (overnight test, 2026-10-21)

## 🟡 Partly working
- [ ] Wi-Fi 2.4 GHz (sees networks, association times out)
- [ ] Modem (boots; no SIM tested, ModemManager doesn't list it) ([[Modem]])
- [ ] Keyboard auto-hide
- [x] Suspend/resume: deep sleep entered automatically and resumed cleanly (Wi-Fi, touch OK); USB after resume untested
- [ ] Android phones can't browse MTP (PC works)
- [ ] OTG: bootable/ISO sticks won't open (no ISO9660/UDF in kernel); exFAT via fuse-exfat
- [ ] NPU userspace (FastRPC library) - CDSP is up
- [ ] NPU first-call race (skel load fails ~1 in 3; apps should retry)
- [ ] Unused clocks/power domains stay on (clk_ignore_unused pd_ignore_unused). Attempt 40 removed them: works but the display probe timed out (reverted). Next: longer deferred_probe_timeout

## ❌ Not done
- [ ] Calls / SMS (needs SIM)
- [ ] Mobile data (no sm8450 IPA in mainline)
- [ ] Speaker protection firmware (louder volume)
- [ ] Sensors (rotate, proximity, light)
- [ ] Vibration: full CS40L26 driver with Samsung firmware + calibration (stronger, custom waveforms)
- [ ] Front camera IMX374: driver written (1616x912 RAW10 @ 30 fps), rails/MCLK/reset/power domain all verified, but CCI bus SDA/SCL held low (timeouts). Next: read the bus on stock Android
- [ ] Rear cameras: S5KGN3 main (QUP i2c@884000), IMX563 ultrawide (CCI0 m0), S5K3K1 tele (CCI0 m1): no mainline drivers
- [ ] Fingerprint (Qualcomm 3D Sonic QBT2000, TrustZone-only: unlikely)
- [ ] NFC
- [ ] DisplayPort out
- [ ] Writable RTC

## 📋 Housekeeping
- [ ] Clean debug leftovers (ath11k debug build, `~/s22-tests`)
- [ ] Battery charge limit control
- [ ] "Landscape" label in Display settings (cosmetic)
- [ ] Upstream pcie-qcom patch (skipped for now)
