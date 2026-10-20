# Boot attempt log

## Attempt 1: uniLoader test image in `boot` (2026-10-17)
Package: out/uniloader-pkg/AP_r0q_uniloader.tar.md5 (boot.img v4, kernel slot = uniLoader 63 MB, no ramdisk, empty cmdline, stock vendor_boot kept).
Flashed with Odin4-GUI, AP slot only.

Result: stuck after the yellow warning screen ("booting kernel" text), no USB device appeared on the PC.

### What the bootloader log says (from TWRP /proc/last_kmsg, saved in notes/logs/last_kmsg3.txt, k3.txt)
- Samsung ABL (build Aug 4 2025) ran "Booting Into Mission Mode", IsUnlocked=1, boot state orange.
- Loaded the whole `boot` partition (0x6000000 = 96 MiB) and `vendor_boot`, `dtbo`.
- vbmeta: hash mismatch but VERIFICATION_DISABLED bit set, so it was allowed.
- `boot` image: no signer/footer, "AUTHENTICATE fail but allow Kernel binary: boot", Knox warranty bit set. NOT blocking.
- dtbo and vendor_boot signatures verified OK.
- ABL applied DTBO overlays, updated the device tree (memory map etc.), "DisableUartDtStatus" (it disables the debug UART /soc/qcom,qup_uart@99c000), drew boot logo.
- Reached `Exit EBS ... UEFI End` (ScmArmV8ExitBootServicesHandler status 0): the bootloader HANDED OFF to our kernel slot (uniLoader).
- Next log entry is a fresh XBL start: "Reset by PWR & Resin S2" = the manual Vol Down + Power force reset. No automatic reset, no watchdog, no panic recorded.

Conclusion: Samsung's bootloader accepts and starts the image. The hang is inside uniLoader or the Linux kernel/initramfs, before any visible output or USB gadget.

### Ideas for next attempts
1. Get kernel output: enable ramoops/pstore console in the DTS (reserved-memory) so TWRP can read /sys/fs/pstore after a reset, and/or set kernel cmdline console=tty0 for the simple framebuffer.
2. Ask the uniLoader r0q authors what boot image layout/cmdline worked.
3. Test pmbootstrap's own boot.img + vendor_boot.img (no uniLoader) in the recovery slot.
4. Add USB debug: check initramfs reaches usb gadget (modules-initfs, dwc3 probe deferrals for regulators).

Restore: out/restore/AP_restore_boot_magisk.tar.md5 (boot), out/recovery/AP_stock_recovery.tar.md5 (recovery).
Note: the recovery slot currently runs a TWRP build for r0q (fingerprint samsung/twrp_r0q/r0q, 2024-07-02).

## Analysis by Opus 5.5 (after attempt 1)
- Final ABL steps before hand-off: BLDP info, "Shutting Down UEFI Boot Services: 7692 ms", "MDPLibProfile: splash enabled"
  (display left running on the splash buffer, so simplefb at 0xb8000000 should be visible), then Exit EBS. Clean hand-off.
- LIKELY CAUSE: the DTB embedded in uniLoader had `memory@a0000000` with size 0 (mainline expects the bootloader to fill it).
  Samsung ABL only fills RAM into its own DTB from vendor_boot. A kernel with no RAM dies before printing anything.
- SECOND RISK: our DTB did not reserve Samsung-specific carve-outs (uh hypervisor heap/guest, kaslr, sec_debug,
  rdx_bootdev, Samsung adsp layout, xbl_ramdump). Touching those can hang silently.

## Changes for attempt 2 (patch: kernel-config/0001-r0q-debug-ramoops-and-console.patch, linux-samsung-r0q pkgrel=1)
- memory node filled with the 5 RAM ranges from the ABL "RAM Partitions" log
- Samsung carve-outs reserved (no-map)
- ramoops at 0x83a000000 (4 MiB) + CONFIG_PSTORE_RAM/CONSOLE/PMSG=y, read from TWRP at /sys/fs/pstore after a reset
- chosen bootargs "console=tty0 ignore_loglevel earlycon"

## Attempt 2 result (2026-10-17)
Package out/uniloader-pkg-attempt2 (RAM ranges + Samsung carve-outs + ramoops, initramfs built into kernel).
Screen went past "Booting Kernel..." to a line mentioning the initramfs and ~9xxxK (initramfs is 9340K), then froze for
several minutes. No USB device on the PC. User forced restart. Bootloader log: same clean path as attempt 1.
Note: TWRP cannot show our ramoops (its DT has no ramoops at 0x83a000000, no /dev/mem), so pstore stays empty there.

## Attempt 3 (canary)
- Kernel cmdline forced (CONFIG_CMDLINE + CMDLINE_FORCE, also DTS bootargs):
  clk_ignore_unused pd_ignore_unused initcall_debug ignore_loglevel console=tty0 panic=0
- Built-in initramfs = pmOS initramfs with /init replaced by a canary that prints "INIT RAN" + counter to /dev/tty0
  and /dev/kmsg every second. Real init kept as /init.pmos. Source: ~/s22-kernel/initramfs/canary

## Attempt 3 result: LINUX BOOTS (2026-10-17)
Photo of screen: full kernel boot with initcall_debug in ~2.4 s, "Hardware name: Samsung Galaxy S22 5G (DT)",
"7.2.11-dirty #3 PREEMPT", "clk: Not disabling unused clocks", then:
  "Warning: unable to open an initial console."
  "Run /init as init process"
  "Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100"  (canary script exited with status 1)
Cause of no visible error: initramfs had empty /dev (unpacked as non-root), so no /dev/console for the script's output.

## Attempt 3b
- Device nodes added at build time via gen_init_cpio list (~/s22-kernel/initramfs/devnodes.list):
  /dev/console, tty0, tty1, null, kmsg. INITRAMFS_SOURCE="canary-dir devnodes.list"
- Canary hardened: output to /dev/console, set -x around mounts, never exits.

## Attempt 3b result: pmOS initramfs reaches debug shell (2026-10-17 ~17:47)
Photo: the REAL pmOS initramfs ran (pmOS-rd messages), not the canary: uniLoader passes blob/ramdisk as initrd and the
kernel unpacks it over the built-in one, so pmOS /init replaced the canary. => uniLoader DOES pass the initrd.
- ~40 s of "couldn't open /dev/input/" (no input drivers yet: no touch/buttons)
- "Creating logs disk", "Mass Storage Function, version: 2009/09/11", "probe of gadget.0 returned 0"
- "Entering debug shell", buffyboard fails (no uinput/input), shell prompt "#" on screen. Device: Galaxy S22 (samsung-r0q),
  Kernel 7.2.11-dirty, initrd 3.12.5-r0.
- PC: NO USB device enumerated at all after the reboot (last event: Download mode disconnect 17:46:13).
Next: diagnostic init passed as uniLoader ramdisk; set up configfs gadget, print /sys/class/udc/*/state and
filtered dmesg (dwc3/phy/udc/gadget/regulator) on screen.

## Attempt 4 (USB diagnostic)
- DTS USB already matches the request and sm8450-hdk.dts: usb_1 dr_mode=peripheral, maximum-speed=high-speed, HS PHY only,
  hsphy supplies L5B 0.88V / L1C 1.8V / L2B 3.072V. Verified identical to stock (pm8350_l5, pm8350c_l1, pm8350_l2). No change.
- New /init (~/s22-kernel/initramfs/diag): mounts, waits 5 s, sets up configfs gadget g1 (18d1:d001, ncm.usb0 or rndis),
  binds the UDC, ifconfig 172.16.42.1, unudhcpd, telnetd :23; then every 5 s clears the screen and prints
  ls /sys/class/udc, UDC state/speed, gadget UDC + iface, and the last 34 dmesg lines matching dwc|usb|phy|udc|gadget|regulator.
- Same archive used built-in AND as uniLoader ramdisk (out/pmos/initramfs), so pmOS init cannot override it.
  Original pmOS ramdisk kept as out/pmos/initramfs.pmos-original.

## Attempt 4 result: USB NETWORKING WORKS (2026-10-17 18:11)
- PC enumerated the phone ~2 min after boot: 18d1:d001 "S22 diag" / postmarketOS / r0q-diag, cdc_ncm -> enxb6f2add9a167.
- Ping 172.16.42.1 works (~4 ms). Phone DHCP (unudhcpd) handed out 172.16.42.2.
- telnet :23 and ssh :22 refused: diag telnetd did not start or died (its error was cleared by the refresh loop).
- HOST NETWORK INCIDENT: `nmcli device connect enx...` moved the user's unbound "Ethernet connection 1" profile off eno1
  onto the phone, dropping wired networking. Fixed: profile restored to NM defaults (never-default no, ignore-auto-dns no,
  ipv6 auto, priority 0) and bound to eno1 (connection.interface-name eno1). New isolated profile "S22 USB (pmOS)":
  ifname enxb6f2add9a167, 172.16.42.2/24 manual, never-default, no DNS, ipv6 disabled.
  NOTE: the enx name follows the gadget's random MAC; set fixed dev_addr/host_addr in the gadget so the name is stable.
- Attempt 4 detail from user: link only came up after the user UNPLUGGED AND REPLUGGED the cable (~2 min after boot).
  A cable already connected at boot is not detected (no VBUS/session event to the UDC at startup).

## Attempt 4b (diag fixes)
- telnetd: main busybox has no telnetd applet; use /usr/bin/busybox-extras telnetd -F -l /bin/sh in a respawn loop,
  all interfaces, log in /tmp/telnetd.log. devpts and tmpfs mounted. /bin/sh symlink added (usr/bin/sh -> busybox).
- Software replug: loop writes disconnect/connect to /sys/class/udc/<udc>/soft_connect until state=configured.
- Fixed NCM MACs: dev_addr 02:22:52:00:00:01, host_addr 02:22:52:00:00:02 -> PC interface enx022252000002.
  NM profile "S22 USB (pmOS)" re-bound to enx022252000002.
- Screen shows UDC state, iface IP, telnetd pid/listening, telnetd.log, replug.log, filtered dmesg.

## Attempt 4b result: ROOT SHELL OVER USB (2026-10-17 18:29)
- Software replug did NOT bring the link up: replug.log shows UDC state 'not attached' at 7 s, 17 s, 27 s...
  A physical cable replug brought it up at once (18:29:09): enx022252000002 with fixed MAC, PC got 172.16.42.2 automatically.
- telnet 172.16.42.1:23 works (busybox-extras telnetd, root /bin/sh). Full dmesg saved: notes/logs/phone_attempt4b.txt
  uname: Linux 7.2.11-dirty #6 SMP PREEMPT aarch64. UDC a600000.usb state=configured after the replug.
- USB drivers fine: 88e3000.phy probe OK at 1.009 s, a600000.usb OK at 1.052 s (after normal -517 deferrals), gadget at 6.1 s.
- Only error in the captured log: arm-smmu 3da0000.iommu (GPU SMMU) probe failed -110, deferred probe timeout
  (expected: GPU clock controller SM_GPUCC_8450 is a module, not loaded).
- Early boot lines were pushed out of the log buffer by initcall_debug (no "Memory:" line captured). Use log_buf_len=4M next time.
- Open: why the host does not see the gadget when the cable is already plugged in at boot (UDC 'not attached' = no bus reset
  from the host). Workaround: replug the cable once after boot.

## Attempt 5 (reboot modes + keys)
- DTS: #include "pmk8350.dtsi" (PMK8350 at SID 0, same as stock): nvmem-reboot-mode mode-recovery=0x01, mode-bootloader=0x02
  (on Samsung "bootloader" should mean Download mode; untested whether Samsung ABL honours these SDAM values).
  pon_pwrkey enabled, pon_resin enabled as KEY_VOLUMEDOWN.
- Kernel: NVMEM_SPMI_SDAM=y, NVMEM_REBOOT_MODE=y, REBOOT_MODE=y (were modules). Cmdline adds log_buf_len=4M.
- Same 4b diag /init. Test plan: from telnet run `reboot recovery` (expect TWRP), later `reboot bootloader` (expect Download).
- Flash route: from TWRP via adb (push boot.img, dd to /dev/block/by-name/boot, verify sha256), user confirms each write.

## Attempt 5 result: BUTTON-FREE REBOOT TO TWRP WORKS (2026-10-17 18:54)
- Full log captured from 0.000 s (log_buf_len=4M): "Machine model: Samsung Galaxy S22 5G",
  "Memory: 6377368K/8205312K available" -> all ~8 GB RAM picked up (RAM fix confirmed). Log: notes/logs/phone_attempt5.txt
- nvmem-reboot-mode probed OK (0.96 s), spmi_sdam0..9 present.
- busybox `reboot recovery` does nothing (busybox reboot ignores the argument, PID 1 is the diag script).
  Built tools/reboot2 (832-byte static, no libc): reboot(LINUX_REBOOT_CMD_RESTART2, "<arg>"). Sent over telnet with base64.
- `/tmp/reboot2 recovery` -> phone in TWRP 27 s later (adb state 'recovery'). Samsung ABL honours SDAM value 0x01.
  `reboot2 bootloader` (0x02, expect Download mode) not tested yet.
- pon_pwrkey / pon_resin did NOT register: parent qcom-pon driver probably still a module. Fix next build.
- Next build ideas (from user): SM8450 GPU/display/camera/video clock controllers =y (removes the 16 s GPU SMMU timeout and
  sync_state pending); MAX77705 (Samsung USB-C/charger chip, has some mainline support) for cable/VBUS detection later.

## Attempt 6 (UFS storage + clock controllers + PON + reboot2)
- DTS: PM8350 S12 (1.224-1.4 V, parent of L6/L9), L6 1.2 V, L7 2.504 V, L9 1.2 V added; vdd-l6-l9-l10-supply = S12.
  &ufs_mem_hc: reset GPIO 210 active-low, vcc L7 (1.1 A), vccq L9 (1.2 A), vdd-hba L9. &ufs_mem_phy: vdda-phy L5, vdda-pll L6.
  All rails/currents/GPIO match the stock S22 DT (pm8350_l7/l9/l5/l6, reset 0xd2) and sm8450-hdk.dts.
- Kernel: SM_GPUCC/DISPCC/VIDEOCC/CAMCC_8450=y, POWER_RESET_QCOM_PON=y (UFS drivers were already =y).
  Risk: dispcc probing could disturb the simple-framebuffer; clk_ignore_unused/pd_ignore_unused should keep it alive.
- Diag init: /usr/bin/reboot2 included; screen shows disk/partition counts and ufs|scsi|sd dmesg lines.
- SAFETY: with UFS visible, Linux can read/write the whole phone flash. The diag does not write to storage.

## Attempt 6 result: UFS STORAGE WORKS (2026-10-17 19:19)
- Written to boot from TWRP over adb (sha256 verified before and after), no Odin. Link came up 45 s after reboot.
- UFS: "scsi host0: ufshcd", MICRON MT128GAXAT2U31, sda 31065088 x 4096 = 127 GB. 6 LUNs (sda..sdf), 98 partitions.
  Only notice: "Unable to find vccq2-supply regulator, assuming enabled" (harmless, vccq2 not used on UFS 3.x).
- Partition map matches Android: boot sda25 (96 MiB), recovery sda26, vendor_boot sda27, super sda29, dtbo sda36,
  efs sda6, sec_efs sda9, persist sda5, metadata sda23, userdata sda40 (106587 MiB), vbmeta sde20.
- Input: pmic_pwrkey and pmic_resin registered. No probe errors; GPU SMMU timeout gone (clock controllers built in).
- DISPLAY NOW FUZZY/GARBLED: disp_cc_sm8450 built in (mdss_gdsc probed at 0.237 s) disturbs the bootloader framebuffer
  under simpledrm. Fix: SM_DISPCC_8450 back to module (or not set) until a real display driver exists.
- Log: notes/logs/phone_attempt6.txt

## Attempt 7
- SM_DISPCC_8450 back to module (attempt-6 photo: noise over the top ~60% of the panel with a vertical split line,
  i.e. panel still scanning but the framebuffer pipeline was reconfigured). Everything else as attempt 6.

## Attempt 7 result (2026-10-17 19:33)
- Screen CLEAR again (dispcc as module fixed the noise). Storage OK (98 partitions), pmic_pwrkey + pmic_resin present,
  no probe errors. Link needed a cable replug (came up 153 s after reboot). Log: notes/logs/phone_attempt7.txt
- Button-free loop confirmed again: reboot2 recovery -> TWRP in 24 s; boot written over adb with sha256 verify.

## Rootfs written to userdata (2026-10-17 ~19:50)
- New `pmbootstrap install` at 19:43 (old image was gone: chroot_native had been cleared by a later pmbootstrap run).
  Image copied to out/rootfs/samsung-r0q.img (1056964608 bytes, sha256 14d0a892...274f91).
  part1 pmOS_boot ext2 uuid 20ef0c96-cdbf-4f90-b6f6-2d13a308938a, part2 pmOS_root ext4 uuid be4488a5-28c6-44b6-adf3-8efca31e0cf0
  (matches the new vendor_boot cmdline). TODO note updated with these UUIDs.
- Written from TWRP: userdata not mounted, /dev/block/by-name/userdata -> sda40 (111765581824 bytes).
  adb push + sha256 match, dd, readback of the first 1056964608 bytes: sha256 match. Android userdata is now gone.

## Attempt 8 (real pmOS init)
- No built-in initramfs (INITRAMFS_SOURCE=""); uniLoader ramdisk = pmOS initramfs from the 19:43 install (out/rootfs/initramfs.pmos).
- Forced cmdline: pmos_boot_uuid=20ef0c96... pmos_root_uuid=be4488a5... pmos_rootfsopts=defaults clk_ignore_unused
  pd_ignore_unused ignore_loglevel console=tty0 panic=0 log_buf_len=4M (also in DTS bootargs).

## Attempt 8 result: pmOS initramfs runs, cannot find rootfs (2026-10-17 19:49)
- pmOS gadget "Galaxy S22 / Samsung / postmarketOS" enumerated (78 s), ping OK, telnet :23 = initramfs debug shell, no ssh.
- pmOS_init.log: "Trying to mount subpartitions for 10 seconds... ERROR: failed to mount subpartitions!"
  blkid: /dev/sda40 PTTYPE="PMBR" (GPT not seen). Log: notes/logs/pmos_initramfs_attempt8.txt
- CAUSE: UFS has 4096-byte logical blocks; image was built with 512-byte sectors; pmOS uses losetup -P --direct-io=on.
- FIX: deviceinfo_rootfs_image_sector_size="4096" (used by 93 pmaports devices), device-samsung-r0q pkgrel=1.
  Needs: pmbootstrap checksum + build device-samsung-r0q, pmbootstrap install (NEW UUIDs), rewrite userdata, new boot image.

## Attempt 8b (4K sectors)
- New install 19:53: image GPT at 4096 (512 none). pmOS_boot uuid BOOT-PARTITION-UUID,
  pmOS_root uuid ROOT-PARTITION-UUID. Initramfs deviceinfo has rootfs_image_sector_size="4096".
  Kernel cmdline updated. Old 512-sector files kept in out/rootfs/old-512/.

## Attempt 8b result: POSTMARKETOS BOOTS FROM STORAGE, SSH UP (2026-10-17 19:59)
- 4K-sector rootfs written to userdata (sha256 bc8703b2... verified), boot image a56416cc... verified, via TWRP/adb.
- After reboot + cable replug: pmOS gadget enx<random MAC> (this boot enxc6cb9d1c7e4e), PC 172.16.42.2 via DHCP,
  eno1 still default route. Port 22 open: "SSH-2.0-OpenSSH_10.6"; password/publickey auth offered. Telnet closed
  (= not in the initramfs debug shell, real rootfs running).
- Notes: the pmOS gadget MAC is random each boot, so the PC interface name changes; the "S22 USB (pmOS)" NM profile is
  re-pointed per boot (fix: fixed MAC via deviceinfo / usb gadget config). Cable replug still needed.

## First SSH session (2026-10-17 20:10) - key auth works (~/.ssh/id_ed25519 on PC, copied with ssh-copy-id)
- Nura edge (postmarketOS is now branded "Nura"), kernel 7.2.11-dirty #11, uptime 10 min, load ~0.
- RAM: 6.1 GiB total, 5.9 GiB available. Root: /dev/loop0p2 auto-resized to 101 GB (153 MB used), /boot loop0p1 450 MB.
- Services started: sshd, swapfile, udev-postmount, local, fsck, root, localmount (zram-swap stopped).
- Input: pmic_pwrkey, pmic_resin.
- Kernel notices: "failed to reserve memory for node 'memory@89900000' (32 MiB)" (overlap with another reserved region,
  investigate: stock cdsp_region is 0x89900000 +32 MiB); "GPT: Use GNU Parted to correct GPT errors" (rootfs image is
  smaller than userdata, backup GPT not at the end; harmless).
- Logs: notes/logs/pmos_first_boot_dmesg.txt, notes/logs/pmos_first_boot_system.txt

## Attempt 9 (fixed USB MAC)
- deviceinfo_usb_network_host_addr="02:22:52:00:00:02" added to device-samsung-r0q (pkgrel=2) and to the booted initramfs
  (~/s22-kernel/initramfs/pmos = pmOS initramfs from the 19:53 install + this line), built in and used as uniLoader ramdisk,
  plus devnodes.list. NM profile "S22 USB (pmOS)" bound to enx022252000002 (DHCP, never-default).

## Research: MAX77705 (cable/VBUS detection, for the "replug" problem)
- Stock S22: max77705@66 on i2c@994000 (QUP geni I2C), irq via PMIC GPIO controller pinctrl@8800 pin 5 (active-low),
  sub-node max77705_pdic (Type-C/PD). Same bus also has fsa4480@42 (audio switch, disabled) and pca9481@57 (charger pump).
- Mainline v7.2.11 has MFD_MAX77705, CHARGER_MAX77705, LEDS_MAX77705, SENSORS_MAX77705, haptics (input/misc) and binding
  mfd/maxim,max77705.yaml. Reference DTS: sdm845-samsung-starqltechn.dts (Galaxy S9: max77705 @0x66 + charger @0x69).
- NO mainline Type-C/PD (pdic) driver for MAX77705, so VBUS/role notification to dwc3 needs either the charger's
  power-supply state wired to the USB controller, or a new pdic/extcon driver. Bigger task; keep the replug workaround for now.

## Attempt 9 result: FIXED USB INTERFACE NAME WORKS (2026-10-17 20:21)
- PC interface enx022252000002 (host MAC 02:22:52:00:00:02) appeared 52 s after reboot, NM profile "S22 USB (pmOS)"
  connected automatically, eno1 stayed default route, SSH with key worked immediately. Passwordless sudo set up on the phone
  (/etc/sudoers.d/90-cammo-nopasswd). Reboot to TWRP from pmOS: ssh ... sudo ~/reboot2 recovery (wrap ssh in a timeout).

## Research: touchscreen and Wi-Fi (2026-10-17 20:25)
- TOUCH: STMicro FTS "stm,stm_ts_spi" on spi@a80000 (CS 0), IRQ tlmm GPIO 46, CS GPIO 31, supplies tsp_io_ldo / tsp_avdd_ldo,
  firmware tsp_stm/fts2ba61y_r0.bin, Samsung "trusted touch" (pvm) config. Mainline only has TOUCHSCREEN_STMFTS (I2C, older FTS).
  => needs a new/ported SPI FTS5 driver. Big job.
- WIFI/BT: QCA6490 ("qcom,cnss-qca6490@b0000000", BT "bt_qca6490" compat qcom,qca6390) on PCIe pcie@1c00000 (pcie0).
  QCA6490 is in the WCN6855 family -> mainline ath11k + hci_qca. sm8450-hdk.dts has the template: wcn6855-pmu power
  sequencer, &pcie0/&pcie0_phy, wifi@0, qcom,wcn6855-bt. Stock firmware in recon/vendor_firmware/qca6490/:
  amss20.bin, m3.bin, regdb.bin, bdwlan.elf* (board files; ath11k wants board-2.bin). Needs Samsung's power rails/GPIOs
  for the chip from the stock cnss node. => most achievable next step.

## No-flash work over SSH (2026-10-17 20:26-)
1. Inventory (notes/logs/pmos_inventory.txt): 72 platform devices with drivers, 28 without. The unbound ones are mostly
   drivers built as modules (none installed yet): spmi-temp-alarm x2, spmi-adc7, trng, qce, tcsr, imem, lpass wsa/rx/tx
   macros + lpi pinctrl, rmtfs-mem, pil-reloc-info, adreno-gmu-730.1, dispcc, rpmh-stats, coresight/ETE.
2. Clock: was 1970-01-01, set from the PC (sudo date -u -s @<epoch>). Lost on reboot until the PMIC RTC driver is in DT.
3. zram swap: service fails, zram is a module -> retry after modules are installed.
4. Modules: building from the same tree/config as the running kernel (7.2.11-dirty) -> install to /lib/modules on phone.
   Blocklist installed: /etc/modprobe.d/s22-bringup.conf (disp_cc_sm8450, msm, qcom_q6v5_pas, qcom_pil_info).
5. Firmware staged (not used yet): /lib/firmware/s22-stock/{qca6490,bt,gpu} (35 files, sha256 verified).
   GPU: a730_sqe.fw, gmu_gen70000.bin (+ zap files); BT: hpbtfw2x.tlv, hpnv2x; Wi-Fi: amss20.bin, m3.bin, regdb.bin, bdwlan*.
6. Internet over USB: pending user approval (changes PC networking).

## Internet sharing over USB (2026-10-17)
- Before: "S22 USB (pmOS)" ipv4.method=auto, never-default=yes, ignore-auto-dns=yes, no static address.
- Undo: nmcli connection modify "S22 USB (pmOS)" ipv4.method auto ipv4.addresses "" ; nmcli connection up "S22 USB (pmOS)"
- Phone side: /etc/local.d/s22-usb-internet.start (default route via 172.16.42.2, resolv.conf, busybox ntpd at boot).
  Verified: ping 1.1.1.1 OK, DNS OK, apk update OK (37177 packages). PC default route unchanged (eno1).

## Module install incident (2026-10-17 20:36)
- Installed 1629 modules (same release 7.2.11-dirty) and ran udevadm trigger --action=add --subsystem-match=platform.
  Autoload candidates: qcom_rng, simple_pm_bus (sram), qcrypto, lpass wsa/rx/tx macros, pinctrl_sm8450_lpass_lpi,
  rmtfs_mem, dispcc_sm8450 (my blacklist had the WRONG name disp_cc_sm8450; fixed to dispcc_sm8450 just before trigger),
  qcom_stats, qcom_spmi_adc5, qcom_spmi_temp_alarm x2, coresight_funnel, socinfo, qcomtee.
- Result: phone hung, display patchy, USB dropped (PC: "device not accepting address, error -71"). Forced off -> TWRP.
- TWRP (Samsung 5.10) cannot mount the pmOS root: ext4 'orphan_file' feature needs >= 5.15.
- Workaround attempt 9b: kernel cmdline adds modprobe.blacklist=qcom_rng,simple_pm_bus,qcrypto,snd_soc_lpass_wsa_macro,snd_soc_lpass_rx_macro,snd_soc_lpass_tx_macro,pinctrl_sm8450_lpass_lpi,rmtfs_mem,dispcc_sm8450,qcom_stats,qcom_spmi_adc5,qcom_spmi_temp_alarm,coresight_funnel,coresight,socinfo,qcomtee,msm,qcom_q6v5_pas,qcom_pil_info

## Module tests (2026-10-17 20:48), one at a time with ping/ssh check after each (logs: notes/logs/module-tests/)
- Attempt 9b booted fine with modprobe.blacklist on cmdline; only coresight trace modules autoloaded. Permanent
  /etc/modprobe.d/s22-bringup.conf now lists 18 modules (all suspects + dispcc, msm, q6v5_pas, pil_info, qcomtee).
- socinfo: OK. qcom_rng: OK.
- qcrypto (qcom,sm8450-qce @0x1dfa000): HANGS THE SOC immediately, USB drops (error -71), same as the 20:36 incident.
  Likely TrustZone-owned on Samsung. KEEP BLOCKED.
- zram: not built (CONFIG_ZRAM unset) -> enable =m in a later kernel build.
- Still to test: qcom_spmi_temp_alarm, qcom_spmi_adc5, rmtfs_mem, qcom_stats, simple_pm_bus, coresight_funnel,
  lpass lpi pinctrl + wsa/rx/tx macros (audio; may hang without LPASS clocks), dispcc (known bad), qcomtee.
- 20:53 retest after reboot: qcom_spmi_temp_alarm OK, qcom_spmi_adc5 OK, rmtfs_mem OK, qcom_stats OK, coresight_funnel OK.
  simple_pm_bus is built in (no module). Not tested on purpose: LPASS audio (needs ADSP), dispcc (known bad), qcomtee.
- Blocklist reduced to 10: qcrypto, dispcc_sm8450, msm, qcom_q6v5_pas, qcom_pil_info, qcomtee, LPASS lpi pinctrl + 3 macros.
- Thermal now readable: ~34 thermal zones (CPU cores, GPU, CDSP, modem, camera, video, mem, PM8350/PM8350C), all ~33-36 C idle.
- Next kernel build: drop modprobe.blacklist from the cmdline (the /etc/modprobe.d file now covers it), CONFIG_ZRAM=m.

## Attempt 10 bundle (in progress, 2026-10-17 ~21:00)
- DTS added (compiles, verified): aliases serial1=&uart20; battery node (3700 mAh, 4.42 V, charge current capped 1.5 A);
  wcn6855-pmu (GPIOs 80/81/82/204 = stock); regulators S10B, S11B, S1C, PMR735A S2E (+ existing S12B); &pcie0 (gen2) +
  &pcie0_phy (L5B/L6B) + &pcieport0 wifi@0 (pci17cb,1103); &qupv3_id_0/&qupv3_id_2; &uart20 + qcom,wcn6855-bt;
  &i2c5: max77705 pmic@66 (IRQ pm8350c gpio5 low), charger@69, fuel-gauge@36 (rsns 5000 uOhm); &pmk8350_rtc okay.
  Pre-bundle DTS backup: /tmp/claude-1000/r0q-before-bundle.dts (also in git diff history of kernel-config patch).
- Kernel: MFD/CHARGER/SENSORS_MAX77705=m, BATTERY_MAX17042=m, ZRAM=m; modprobe.blacklist removed from cmdline.
- Phone blocklist now 22 entries incl. all new bundle modules (pcie phy, pwrseq, pwrctrl, ath11k, hci_uart, i2c geni,
  max77705, charger, fuel gauge, hwmon, rtc). Test them one at a time over SSH after flashing.
- Phone firmware: apk linux-firmware-ath11k (WCN6855 hw2.0/hw2.1) + linux-firmware-qca (hpbtfw21.tlv, hpnv21.*) installed.
- memory@89900000 (cdsp_mem) note: reservation fails because 0x8b7cf000-0x8b849fff is reserved earlier (likely the
  uniLoader DTB copy); region shows as System RAM. Harmless until CDSP is used; must fix before DSP work.
- NEXT: build (bw6ics2dp running: Image+dtb+modules) -> install modules over SSH -> user runs build-uniloader.sh ->
  package attempt 10 -> reboot2 recovery -> write boot (user OK) -> boot -> test modules one by one.

## Attempt 10 result + no-flash tests (2026-10-17 21:28-21:36)
- Boots fine with the full bundle DT. zram module autoloads (swap service still fails: investigate).
- rtc_pm8xxx: /dev/rtc0 works but RTC holds 1970 and is READ-ONLY (RTC_SET_TIME -> ENODEV; firmware owns it, real time is
  an offset elsewhere). It reset the system clock to 1970 on load -> blocked again; NTP keeps the clock right.
- i2c_qcom_geni OK (/dev/i2c-0 = i2c5). MAX77705 core: mainline only accepts PASS3; S22 chip is Rev 0x2. Applied upstream
  "[PATCH v1] mfd: max77705: support revision 0x2" (kernel-config/upstream-patches/max77705-v1-support-rev2.patch), rebuilt
  only that module, installed on the phone (orig kept as max77705.ko.zst.orig): core probes OK.
- Fuel gauge driver waits for the charger device (power-supplies link). Read raw over I2C instead (i2cget -f 0 0x36):
  RepSOC 100 %, VCell 4336 mV, +18 mA, 29 C (matches the bootloader's own reading). Charger driver left blocked (user decision).
- Upstream r0q v2 DT uses interrupts-extended <&tlmm 5> for the MAX77705; stock DT says PM8350C GPIO 5 (ours). Note if IRQs misbehave.
- Wi-Fi: phy_qcom_qmp_pcie OK -> pwrseq_qcom_wcn OK -> pci_pwrctrl_pwrseq OK -> PCIe link up, lspci:
  "01:00.0 Network controller: Qualcomm QCNFA765 Wireless Network Adapter (rev 01)" (17cb:1103).
  ath11k_pci: "wcn6855 hw2.1", MHI power on OK, then "MHI did not load image over BHI, ret: -5" -> -110. Same with the phone's
  own stock firmware in /lib/firmware/updates/ath11k/WCN6855/hw2.1 (amss20.bin->amss.bin, m3.bin). => not a signature
  problem; suspect DMA/IOMMU/hypervisor permission for PCIe or a missing PCIe property. Phone stays stable.
- Bluetooth: hci_uart -> "setting up wcn6855", downloaded qca/wcnhpbtfw21.tlv + wcnhpnv21g.bin,
  "BTFW.HSP.2.1.0-00660", "QCA setup on UART is completed", hci0 present. BLUETOOTH WORKS.
- Blocklist now: qcrypto, dispcc, msm, q6v5_pas, pil_info, qcomtee, 4x LPASS, ath11k_pci, ath11k, max77705_charger, rtc_pm8xxx.
- Bluetooth address: bootloader passes none, hci0 was "unconfigured". Real address from EFS backup /bluetooth/bt_addr
  (48:BC:E1:XX:XX:XX, read from efs-backup/efs.img with debugfs, phone EFS untouched). Set with btmgmt public-addr ->
  bluetoothd sees "Controller 48:BC:E1:XX:XX:XX (public)", powered. Persisted: /etc/local.d/s22-bt-addr.start, bluetoothd in default runlevel.
  Discovery finds 0 devices: stock BT uses 5th supply qcom,bt-vdd-asd-supply = PMR735A L7 2.8 V (antenna switch; Wi-Fi
  uses it too as wlan-ant-switch). NOT in our DT -> add in attempt 11 (always-on regulator).
- zram: pmOS zramstart wants zstd; zram had only lzo. Enabled ZRAM_BACKEND_ZSTD/LZ4, rebuilt only zram.ko (needs syncconfig
  + clean M= build), installed: comp_algorithm "lzo-rle lzo lz4 zstd", 9.2 GB zram swap active. No flash.

## Attempt 11 (touch + antenna switch + CDSP memory + 4.35 V)
- Touch: applied upstream "[PATCH v1 1/2] dt-bindings ... FTS2BA61Y" and "[PATCH v1 2/2] Input: add support for the STM
  FTS2BA61Y touchscreen" (Eric Goncalves / Ivaylo Ivanov, Sept 2025, not merged in 7.2.11). CONFIG_TOUCHSCREEN_FTS2BA61Y=m,
  CONFIG_SPI_GPIO=m. DT from upstream r0q v2 3/6: spi8 = spi-gpio on GPIO 30/29/28/31 (sck/mosi/miso/cs), touchscreen@0
  st,fts2ba61y, IRQ tlmm 46 low, vdd L8C 1.8 V, avdd L11C 3.0 V (both match stock tsp_io_ldo/tsp_avdd_ldo), pin states.
  fts2ba61y blocked on the phone until tested by hand.
- Antenna switch: PMR735A L7 2.8 V always-on (stock wlan-ant-switch / qcom,bt-vdd-asd), vdd-l7-bob from BOB.
- CDSP memory: &cdsp_mem shrunk to 0x89900000+2 MiB, plus 15 x 2 MiB no-map "cdsp-mem-part" nodes, so the early reservation
  at 0x8b7cf000 only knocks out one piece. RESTORE a full 32 MiB cdsp_mem before CDSP remoteproc work.
- Battery: voltage-max-design 4.35 V (was stock 4.42 V), charge current still capped 1.5 A. Charger driver still blocked.
- Note: adding a new Kconfig symbol makes make (syncconfig) stop at an interactive (NEW) prompt -> always run
  scripts/config + make olddefconfig </dev/null before building.
- Fix: upstream fts2ba61y v1 calls usleep(1) (no such kernel function, build error) -> fsleep(1).

## Attempt 11 result (2026-10-17 22:06)
- Boots fine. CDSP split works as designed: only cdsp-mem-part@8b700000 fails (early reservation now at 0x8b7d1000);
  the other 30 MiB are nomap. Antenna switch ldo7 enabled at 2.8 V. spi0.0 (touch) present. zram 9.2 G active at boot.
- TOUCH WORKS: fts2ba61y -> input2/event2. evtest 41 s: 13340 events, 55 touches, X 37..3924, Y 222..3967 (range 0..4095,
  = stock sec,max_coords 0x1000), pressure 2..63. Only slot 0 seen (multitouch not exercised/confirmed yet).
  Needs coordinate mapping 4096x4096 -> 1080x2340 for a UI.
- BLUETOOTH SCAN WORKS: after setting public-addr, btmgmt find -> 10 devices. Antenna switch was the missing piece.
  Boot script did not apply the address (ran before QCA setup finished) -> rewritten to retry until "complete" (90 s).
- Wi-Fi still fails: chip enumerates (QCNFA765), MHI power on, then "Device link is not accessible / MHI did not enter
  READY state" (-110). CDSP memory fix + antenna switch did not change it. Open: compare stock pcie0/cnss properties
  (qcom,wlan-cbc-enabled, smmu/sid, ep settings), PCIe link speed gen1, iommu passthrough test.
- Touch orientation (corner taps, portrait): TL (262,272) TR (3811,282) BR (3882,3938) BL (311,3965) centre (2096,2232).
  => X left->right, Y top->bottom, no swap/mirror, full 0..4095 on both axes = whole screen. libinput maps the full
  range to the output, so no LIBINPUT_CALIBRATION_MATRIX needed. udev: ID_INPUT_TOUCHSCREEN=1, INPUT_PROP_DIRECT set.
- MULTITOUCH WORKS: 2 simultaneous contacts seen, slots 0/1/2 used (log: module-tests/a11-touch-corners-mt.txt).
- Wi-Fi lead tested: stock pcie0 has qcom,target-link-speed = <1> (Gen1). Forced root port LnkCtl2 target 2.5 GT/s + retrain
  (setpci), link at 2.5 GT/s -> ath11k fails the same way. Link speed ruled out.
  Ruled out so far: fw signature, ASPM, IOMMU faults, CDSP memory, antenna switch, link speed.
  Remaining leads: stock cnss uses an AOP mailbox (mboxes = <0x5d 0>) for QMP messages; qcom,vreg_ipa = "s3e" (PMR735A S3);
  Samsung PCIe driver qcom,pci-msm with its own PHY sequence vs mainline settings. Stock pcie0: dma-coherent, iommu-map 0x1c00.

## Phosh desktop test (2026-10-17 22:10-22:25)
- apk add postmarketos-ui-phosh (854 packages, 1.3 GB). Enables greetd (phrog greeter) + elogind at boot.
- Fix 1: no GPU -> wlroots refuses software GL ("Software rendering detected", EGL/Vulkan fail). Use pixman:
  /etc/phrog/greetd-config.toml command = "env WLR_RENDERER=pixman /usr/libexec/phrog-greetd-session" (orig .orig),
  and WLR_RENDERER=pixman in /etc/environment (for the user Phosh session via pam_env).
- Fix 2: on-screen keyboard crashed ("Settings schema 'mobi.phosh.osk' is not installed"), killing the required
  gnome-session component: apk add stevia-schemas (package renamed from phosh-osk-stevia; schemas are separate).
- Fix 3: phoc "libseat: Could not take device: No such device" for event0-2: input devices had no udev seat tags
  (created before udev). udevadm trigger --action=add for input/drm/graphics fixes it. Persisted as OpenRC service
  /etc/init.d/s22-seat-retrigger (before elogind/greetd, default runlevel).
- Touch driver fts2ba61y unblocked (tested OK) so it autoloads at boot.
- RESULT: phrog (Phosh login screen) running as greetd: phoc + gnome-session + stevia OSK + phrog, touch events reach phoc
  ("event processing lagging" = slow CPU rendering). Harmless warnings: no audio (Gvc), no NetworkManager agent, no modem.
- Mistake noted: `pkill -f <pattern>` over ssh matched my own shell's command line and killed the ssh session. Use PIDs.
- User wants a numeric password for Phosh; suggested `ssh -t cammo@172.16.42.1 sudo passwd cammo` (typed by the user).
- 22:28 USER LOGGED IN TO PHOSH on the phone (numeric PIN set by the user). Session c34: cammo seat0 tty7 active;
  phoc, phosh, gnome-session (phosh), stevia OSK, xdg-desktop-portal-phosh running. Load 0.08, 435 MB RAM used.
- UI scale: phoc defaulted to scale 1.0 (simpledrm reports bogus 285x619 mm). Live: wlr-randr --output Unknown-1 --scale 2.5.
  Persisted in /etc/phosh/phoc.ini and /etc/phrog/phoc.ini ([output:Unknown-1] scale = 2.5).
- Icons/apps: gdk-pixbuf 2.44 delegates image loading to glycin (sandboxed with bwrap); glycin-image-rs/glycin-svg present,
  bwrap sandbox works (USER_NS/SECCOMP ok). Added GSK_RENDERER=cairo to /etc/environment (GTK4 apps without GPU), effective
  on next login. Settings (gnome-control-center) ran 20 s without crashing in a test. Enabled busybox syslog (/var/log/messages)
  to capture app crashes.

## Attempt 12 (GPU render-only) - DT only, kernel identical to attempt 11
- No mainline panel driver for the S22 S6E3FAC (only s6e3fa7, s6e3fc2x01, ...), so keep scanout on simple-framebuffer and
  use the Adreno 730 render-only: msm module option separate_gpu_kms=1 (/etc/modprobe.d/s22-gpu.conf). MDSS stays disabled.
- DT: &gpu status okay (qcom,adreno-730.1; gmu, adreno_smmu, gpucc already enabled in sm8450.dtsi);
  &gpu_zap_shader firmware-name = "qcom/sm8450/samsung/r0q/a730_zap.mdt".
- Firmware on phone (stock, sha256 verified): Samsung-signed zap a730_zap.mdt/.b00-.b02/.elf from /vendor/firmware_mnt/image
  -> /lib/firmware/qcom/sm8450/samsung/r0q/; a730_sqe.fw + gmu_gen70000.bin -> /lib/firmware/updates/qcom/.
  msm catalog: chip 0x07030001, sqe a730_sqe.fw, gmu gmu_gen70000.bin, zap a730_zap.mdt. Mesa msm_dri.so (freedreno) present.
- msm still blocked; test by hand after flash. Risk: zap auth through Samsung TZ (cf. qcrypto hang).

## Attempt 12 result: GPU WORKS, PHOSH RENDERS ON THE ADRENO 730 (2026-10-17 22:40)
- Boot: 3d00000.gpu, 3d6a000.gmu present; 3da0000.iommu (adreno_smmu) now binds arm-smmu (old 16 s timeout gone).
- modprobe msm (separate_gpu_kms=1): "[drm] Initialized msm 1.13.0 for 3d00000.gpu on minor 1", loaded qcom/a730_sqe.fw and
  qcom/gmu_gen70000.bin (stock, /lib/firmware/updates), "Loaded GMU firmware v4.0.7". /dev/dri/renderD128 + card1 created.
- First GPU use (eglinfo -B -p surfaceless): freedreno FD730, OpenGL 4.6 Core, Mesa 26.2.4. Samsung TZ ACCEPTED the
  stock-signed zap shader (qcom/sm8450/samsung/r0q/a730_zap.mdt). No kernel errors, no hang.
- msm unblocked (autoloads at boot). /etc/environment: WLR_RENDERER=pixman -> WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128
  (pixman version saved as /etc/environment.pixman); greetd command uses the same. Scanout stays on simpledrm (card0).
- After greetd restart: /sys/kernel/debug/dri/1/clients shows phoc (4 handles) + Xwayland on renderD128 => GPU compositing.
- OSK (stevia) scaling setting is 'auto-portrait','auto-landscape'; it sizes on start, so a restart after the 2.5 scale change
  should fix the user's "OSK needs scaling". GSK_RENDERER=cairo still set for GTK4 apps (can switch to GL later).
- User: "much faster" with GPU compositing. Issues: OSK tiny (like a nav bar), no visible home bar.
  Cause: simpledrm reports 285x619 mm (96 dpi default); stevia 'auto' scaling sizes keys from physical mm.
  Workaround (no flash): gsettings set mobi.phosh.osk scaling "[]" (user session) + restart stevia.
  Proper fix staged for next flash: simple-framebuffer `panel = <&panel_size>` with panel-size { width-mm = <65>;
  height-mm = <140>; } (simpledrm reads width-mm/height-mm from the panel phandle). Home bar likely hidden under the OSK.
- No home bar: phosh only treats outputs named DSI-/eDP-/LVDS- as built-in; simpledrm connector is "Unknown-1" so
  phosh treated it as external (no phone home bar). Fix: PHOSH_DEBUG=fake-builtin in /etc/environment. Also user got stuck
  in Settings without a home bar -> closed gnome-control-center over SSH.

## Plasma Mobile install (2026-10-17 22:50)
- apk add postmarketos-ui-plasma-mobile: OK (1185 packages, 2.7 GB). kwin_wayland present; sessions: phosh, plasma-mobile, plasma.
- It added tinydm to the default runlevel next to greetd (two display managers would fight) -> rc-update del tinydm default.
  greetd/phrog stays; pick "Plasma Mobile" in phrog's session list. Note: with scanout on simpledrm (no render node) KWin
  can only do software (QPainter) compositing, so Plasma will be slow until the real panel driver works.

## Attempt 13 (panel driver + Wi-Fi rails) - DTB only, kernel image = attempt 11 (staged, NOT flashed)
- New driver drivers/gpu/drm/panel/panel-samsung-s6e3fac.c (CONFIG_DRM_PANEL_SAMSUNG_S6E3FAC=m, copy in kernel-config/).
  From lmdpdg output (fhd60hs): command mode (no MIPI_DSI_MODE_VIDEO), 4 lanes, DSC 1.1 2x 540x117 slices 8bpc/8bpp,
  PPS sent once inside on() (stock order: compression mode -> PPS), display on/off = stock samsung,display_on/off_tx_cmds
  (9f a5a5 / 29 or 28 / 9f 5a5a), backlight DCS 0x51 11-bit (init writes 0x07ff), supplies vddio/vdd3/vddr/vci.
- Stock panel wiring (recon/live.dts): reset GPIO 34 (reset-sequence <1 10> -> GPIO_ACTIVE_LOW), TE GPIO 86 (mdp_vsync),
  panel_vdd3 = fixed reg GPIO 21 (1.8 V), panel_vddr = GPIO 62 (1.3 V), panel_vci = GPIO 20 (3.0 V), display vddio =
  pm8350c L12 1.8 V (stock always-on). DSI ctrl vdda = L6B, PHY vdds = L5B (same as sm8450-hdk). Panel 64 x 140 mm.
- DT: &mdss, &mdss_dsi0 (+ panel@0 samsung,s6e3fac-amb606aw01), &mdss_dsi0_out, &mdss_dsi0_phy enabled; vreg_l12c_1p8
  always-on; 3 GPIO fixed regulators boot-on + always-on (panel keeps power if DSI does not probe); pin states for
  GPIO 20/21/34/62/86. simple-framebuffer panel-size fixed to 64 mm. Backup: /tmp/claude-1000/r0q-before-panel.dts.
- SAFE BRING-UP: dispcc_sm8450 stays blacklisted, so MDSS/DSI just defer and boot is identical to attempt 12 (simpledrm).
  Test by hand: sudo modprobe dispcc_sm8450 -> msm KMS binds MDSS -> DSI host -> panel probes; msm kicks simpledrm out.
- Panel module installed on the phone (/lib/modules/7.2.11-dirty/kernel/drivers/gpu/drm/panel/, depmod done).
- Expected bonus: connector becomes DSI-1, which phosh treats as built-in (home bar), and real mm values for scaling.

## Wi-Fi: rail voltages (2026-10-17 23:10)
- Stock cnss-qca6490 rails vs ours (hdk template): vdd-wlan-aon = pmr735a_s2 1.012 V (ours S2E sat at 0.500 V, the DT min!),
  rfa2 = pm8350_s12 1.35 V (ours 1.224 V), rfa1 = pm8350c_s1 1.9-2.1 V (ours 1.808 V), dig = s11b 0.966 V (ok), io s10b ok.
- Live test (no flash): scratch module wifirails.ko (regulator_get(NULL,"smps2") etc.) raised them to 1.012/1.352/1.904 V,
  then modprobe ath11k_pci -> same "MHI did not load image over BHI, ret: -5". But the chip was already powered at boot on
  the starved rail. Power-cycle attempt: removing 01:00.0 + unbinding pci-pwrctrl-pwrseq -> device did not come back on
  rescan (link does not retrain); qcom-pcie has no unbind -> needs a reboot (and a USB replug, so left for the user).
  Logs: notes/logs/module-tests/a12-wifi-stock-rails*.txt. Phone left running, AER correctable RxErr only.
- Attempt 13 DT: S2E min 1.012 V, S12B min 1.35 V, S1C min 1.9 V (stock values) so the chip powers up at the right voltages.

## Attempt 13 result (2026-10-17 23:40): REAL PANEL DRIVER WORKS
- Flashed boot only (sha256 258005e6...059b0, readback OK). Boot screen black: simple-framebuffer never probed,
  "wait for supplier /panel-size" -> fw_devlink treats `panel = <&panel_size>` as a supplier and panel-size never becomes
  a device. => REMOVE the panel property from the framebuffer node next DT build. msm-mdss gave up (deferred probe timeout -110).
- Wi-Fi with stock rails from power-on (S2E 1.012 / S12B 1.352 / S1C 1.904 V confirmed): still fails, now
  "Device link is not accessible / MHI did not enter READY state" -110. Rails ruled out (kept at stock values).
- Display: modprobe dispcc_sm8450 + bind ae00000.display-subsystem to msm-mdss -> "bound ae94000.dsi", DPU rev 0x80010000,
  msm-kms on card1, card1-DSI-1 connected 1080x2340, panel_samsung_s6e3fac probed, backlight ae94000.dsi.0, fbcon on msm.
- greetd: WLR_DRM_DEVICES=/dev/dri/card1 (+ renderD128) in greetd-config.toml and /etc/environment; [output:DSI-1] scale 2.5
  in both phoc.ini. phrog greeter runs on the real panel. dispcc_sm8450 unblocked (backup s22-bringup.conf.a12).

## DISPLAY WORKS (2026-10-18 ~00:00) - real S6E3FAC panel driver, frames straight to the panel
- Panel read back OK from the start (ID 81 00 65, power mode 0x9d = booster/sleep-out/normal/display-on) but stayed black.
- Reference: Samsung kernel sources (SM-S901E ykasidit/s22_kernel_SM-S901E, SM-S901U polygraphene/SM-S901U_NA_12_Kernel,
  in ~/s22-kernel/ref/, sparse checkouts of display-drivers/msm/samsung/S6E3FAC_AMB606AW01 + cnss2).
  ss_brightness_gamma / samsung,gamma_mode2_normal_tx_cmds: b0 00 0d 94; 94 70 00; 53 28 (WRCTRLD BCTRL on); b0 00 01 b1;
  b1 14 (TSET); 51 hi lo; b0 02 79 92; 92 <IRC>; f7 0f.  Our init never sent 0x53 -> black.
- Fix: init now sends 94 20 00 / 53 20 / b1 14 / 51 03 ff; backlight update = f0 5a5a, 53 20, 51, f7 0f, f0 a5a5.
  After reboot: user sees the UI, "so much snappier". dispcc unblocked -> display comes up by itself at boot.
- CRASH: hot-reloading the panel module (unbind msm-mdss while greetd stopped) oopsed and hung jbd2/kworkers -> forced
  restart. Never unbind msm-mdss live; install module + reboot instead. pstore had only pmsg (no dmesg oops record).
- `~/reboot2` with no argument rebooted into TWRP (stale reboot reason). Use `adb reboot` from TWRP; pass an explicit arg.
- Plasma: phrog has no visible session switch on the first page; set greetd's gsettings mobi.phosh.phrog last-session
  'plasma-mobile' (sudo -u greetd dbus-run-session gsettings set ...), greetd restarted.
- Wi-Fi (attempt 13, stock rails from power-on): still "MHI did not enter READY state" -110. Next: compare with Samsung cnss2.

## Wi-Fi: comparison with Samsung S901E kernel (2026-10-18) - NOT SOLVED
- Samsung cnss2 (drivers/net/wireless/cnss2) power-up compared with mainline pwrseq-qcom-wcn: XO clk -> WLAN_EN order, 100 ms BT_EN rule,
  AOP mbox (vreg_ipa s3e enable is sent only AFTER fw boot, qmi.c:805) -> no difference that blocks BHI. Rails fixed to stock (S2E 1.012 V etc.).
- Samsung pci-msm (drivers/pci/controller/pci-msm.c) vs mainline pcie-qcom: PARF_SYS_CTRL written 0x365E; live mainline value 0x361c
  (missing CORE_CLK_CGC_DIS + L1_CLK_RMV_DIS). Set live via /dev/mem (PARF @0x1c00000): no change.
- ASPM/L1ss all disabled live (sysfs link/*): no change.
- BHI observations (mhi built with -DDEBUG, python mmap of resource0): chip EE=PBL, MHISTATUS READY; PBL resets itself ~1 s after power-on
  then idles in PBL; "Starting image download via BHI" then ~20 s later link-fatal / -EIO. No SMMU/AER messages. Once PBL reported BHI
  STATUS=SUCCESS with matching session id (EE stayed PBL).
- Not yet compared: SMMU/IOMMU path (stock pcie0: dma-coherent, iommu-map 0x1c00/0x1c01, qcom,iommu-dma-addr-pool), PHY init table.
- Debug mhi.ko (-DDEBUG) is loaded on the phone via /tmp/mhi-debug.ko only (not installed); Makefile original in /tmp/claude-1000/mhi-host-Makefile.orig.

## Attempt 14 (pcie0 dma-coherent) result (2026-10-18 00:25)
- Flashed boot only (sha256 5aa30ea3...16ce3be, readback OK). Wi-Fi unchanged: "Device link is not accessible / MHI did not enter READY state" -110.
  dma-coherent ruled out (kept in DT, harmless). Remaining: SMMU address pool (qcom,iommu-dma-addr-pool 0xa0000000+256M, fastmap), PCIe PHY init diff.
- Clock: RTC unusable -> /etc/init.d/s22-clock (boot runlevel) restores /var/lib/s22-time at boot (never backwards), saves at stop;
  /etc/local.d/s22-clock-save.start saves every 2 min; chrony makestep 1 -1. Time file seeded 1792243083.
- Sessions: phosh.desktop + plasma.desktop moved to /usr/share/wayland-sessions.disabled/ -> only Plasma Mobile listed in phrog.
  phrog/greetd itself still depends on Phosh libs, so Phosh packages stay installed (switch to tinydm later to remove them).
- Battery %: /sys/class/power_supply empty (max17042 child of MAX77705 fails "power supply register"). Samsung reference:
  Kernel/kernel_platform/msm-kernel/drivers/battery/common/sec_battery*.c and the max77705 fuel gauge in the S901E repo.

## Attempt 15 (staged, NOT flashed): fuel gauge without charger dependency
- /sys/class/power_supply empty: max77705-battery (max17042) defers because DT fuel-gauge@36 has power-supplies = <&max77705_charger>
  and max77705_charger is blocked. Removed that property (backup /tmp/claude-1000/r0q-before-fg.dts). Expect a "battery"/max170xx supply with real %.

## Charger + Plasma-only login (2026-10-18 ~00:40) - live, no flash
- max77705_charger is an I2C driver (charger@69 node, regmap on 0x69), NOT an MFD cell (the "Failed to locate of_node" message is the unused MFD cell).
  modprobe max77705_charger works: supply "max77705-charger" type USB online=1 status=Full (battery 99 %, 4.337 V). Unblocked in s22-bringup.conf.
  Can't prove charging until the battery drops below full. Battery supply status stays "Unknown" because DT fuel-gauge has no power-supplies link
  to the charger (re-add <&max77705_charger> in the next DT now that the charger loads).
- Login: switched greetd(phrog) -> tinydm autologin (uid 10000 = cammo, /etc/conf.d/tinydm), tinydm-set-session plasma-mobile.desktop.
  rc-update: del greetd default, add tinydm default. Plasma (kwin_wayland + plasmashell) came up. Rollback: rc-service tinydm stop;
  rc-update del tinydm default; rc-update add greetd default; rc-service greetd start. Phosh packages still installed (remove after confirm).
- Autologin = no PIN at boot; the screen locker (kscreenlockerrc) still locks later.

## Wi-Fi tests 2026-10-18 (no flash) + Attempt 16 (staged)
- Charger works live (battery went 99 -> 100 %). tinydm autologin -> Plasma only.
- ath11k_pci rebuilt WITHOUT ath11k_pci_soc_global_reset (Samsung cnss2 never does it): same "Device link is not accessible" (log a15-wifi-noreset.txt). pci.c restored.
- Attempt 16 DT (STAGED, not flashed): wifi@0 label wifi_dev + memory-region -> reserved-memory wifi-iova-pool with
  iommu-addresses <&wifi_dev 0 0 0 0xa0000000>, <&wifi_dev 0 0xb0000000 0 0x50000000> (leaves only stock window 0xa0000000+256 MiB;
  mainline gave BHI buffer IOVA 0xffa00000). Plus fuel-gauge power-supplies = <&max77705_charger> again (charger loads now).
  Backup /tmp/claude-1000/r0q-before-iova.dts. Odds are modest: hdk uses top-of-4G IOVAs fine.

## Attempt 16 result (2026-10-18 00:40)
- Flashed boot only (sha256 d7432aef...86f68, readback OK). Reboot test PASSED: tinydm autologin -> kwin_wayland + plasmashell up by themselves,
  clock correct (restore service works), battery status Full 99 % (gauge linked to charger), charger online.
- Wi-Fi: IOVA window reservation (stock 0xa0000000+256 MiB) made NO difference: "MHI did not load image over BHI, ret: -5". 
  Ruled out for Wi-Fi so far: rails, ASPM/L1ss, link speed, PARF_SYS_CTRL, dma-coherent, SoC global reset, IOVA window, firmware files, antenna switch.

## Wi-Fi: PCIe PHY init table vs Samsung stock (2026-10-18) - staged, no flash needed
- Stock DT pcie@1c00000 carries the full `qcom,phy-sequence` (offsets from PHY base 0x1c06000). Diffed against mainline
  sm8450_qmp_gen3x1 tables (serdes/tx/rx/pcs/pcs_misc + rc tables): identical except 3 board-tuning registers:
  TX RES_CODE_LANE_OFFSET_TX (0xe3c) stock 0x12 vs 0x16, TX RES_CODE_LANE_OFFSET_RX (0xe40) 0x01 vs 0x04,
  RX VGA_CAL_CNTRL2 (0x10d8) 0x0f vs 0x07. Fits the correctable RxErr AER + link loss under BHI DMA load.
- Patched drivers/phy/qualcomm/phy-qcom-qmp-pcie.c (orig: .c.orig / .ko.orig), rebuilt only phy-qcom-qmp-pcie.ko
  (sha256 bd55bccd...c045, vermagic 7.2.11-dirty). Install on phone over the existing .ko.zst (keep .orig), reboot, modprobe ath11k_pci.
- Result (a17, 08:29): stock PHY values confirmed live via /dev/mem (0xe3c=0x12, 0xe40=0x01, 0x10d8=0x0f), Wi-Fi unchanged:
  "Device link is not accessible / MHI did not enter READY state" -110 (log a17-wifi-phy-stock-tuning.txt). PHY tuning ruled out.
  Patched module left installed (matches stock, harmless); revert = phy-qcom-qmp-pcie.ko.zst.orig on the phone.

## Wi-Fi ROOT CAUSE #1 FOUND (2026-10-18 08:30-08:50): no MSIs from the chip - mainline BDF->SID bug
- a18: /proc/interrupts during ath11k load: bhi/mhi/ce all 0 (root port bwctrl fires). MSI config fine (both in iommu group 9, doorbell 0xfffff040).
- a19: mhi.ko patched to POLL BHI/BHIe status (boot.c, orig boot.c.orig; test module ~/mhi-poll.ko on phone, NOT installed):
  "BHI poll: status 2 after 40 ms" -> Device in READY -> BHI never was broken, only its interrupt.
- PARF BDF_TO_SID table (0x1c02000): only slot 7 = 0xffe54300 (junk), 01:00.0 slot 21 empty. Cause: qcom_pcie_config_sid_1_9_0()
  parses iommu-map as 4 cells/entry, but sm8450 apps_smmu has #iommu-cells=2 (5 cells/entry) -> 2nd entry misread
  (bdf=1, sid=smmu phandle -> (0x43-0x1c00)<<8 overflow = 0xffe54300). Endpoint then uses root port SID 0x1c00 -> its MSIs hit the
  ITS as DeviceID 0x5980 (root port) and are lost. DMA still works (same iommu domain).
- a20/a21: writing slot 21 = 0x01000100 by hand (/dev/mem) -> bhi IRQs arrive (4). Clean boot + fix: BHI OK, READY, then
  ~0.6 ms after MHI register init: "System error detected / Controller supports RDDM, skip SYS_ERROR" -> SBL crashes (new, real error).
- TODO: proper fix in pcie-qcom.c (stride from #iommu-cells; pcie-qcom built in -> kernel rebuild + boot flash). Investigate SBL SYS_ERR.

## Attempt 17 (2026-10-18 08:55): pcie-qcom BDF->SID fix FLASHED
- pcie-qcom.c qcom_pcie_config_sid_1_9_0(): iommu-map entry length now 3 + #iommu-cells (orig pcie-qcom.c.orig). Kernel #18
  (only other .config diff vs #16: DRM_PANEL_SAMSUNG_S6E3FAC=m). DTB unchanged. boot.img sha256 3b1f3e64...aed957, readback OK.
  Old kernel kept: out/pmos/vmlinuz.a16-build16.
- Verified: BDF_TO_SID slot 21 = 0x01000100 by itself, junk slot 7 gone.
- Stock mhi.ko (no polling): BHI now completes via interrupt (bhi IRQ 3), "Wait for device to enter SBL or Mission mode",
  then times out (SBL SYS_ERR seen in a21). NEXT: why SBL crashes (read RDDM / BHI error regs, compare with Samsung cnss2).
- a22 (SBL crash regs, read from BAR0 at the SBL wait): MHISTATUS 0xff05 (state SYS_ERR, syserr=1), BHI EXECENV 1 (SBL),
  ERRCODE 0, ERRDBG1 0xfef00000, ERRDBG2 0x0144657c, ERRDBG3 0x2c. No SMMU faults.
  Same with upstream linux-firmware WCN6855 hw2.1 (stock dir moved aside, then restored) -> not firmware-specific.
- a23: ath11k MHI iova_start/stop = Samsung window 0xa0000000+256 MiB -> same SBL SYS_ERR. Ruled out.
- Test sources restored (ath11k/mhi.c, mhi/host/boot.c from .orig). Test modules only in ~ on the phone (mhi-poll.ko, ath11k_pci-iova.ko).
  NOTE: Module.symvers had been clobbered by single-target builds -> always run full `make modules` (incremental) before M= builds.
- a24: cnss2-style SRAM dump at the SBL crash (BAR window 0x310c): TCSR_PBL_LOGGING 0x5b1e21ed, PBL_WLAN_BOOT_CFG 0x1,
  PBL_BOOTSTRAP 0x42, QFPROM_PWR_CTRL 0x62; PBL log (0x01403DA0) all 0; "SBL log" (ERRDBG2/3) all 0 but one word 0x81446800. No message.
  cnss2 wlaon pwr ctrl only with qcom,set-wlaon-pwr-ctrl (not in stock DT) -> irrelevant. MHI event rings equal (cnss adds BW-scale ring only).
- a25: scratch rfclk.ko voting RPMh RF_CLK1-4 on (sm8650 boards give the PMU RF_CLK1) -> same SBL SYS_ERR. Unloaded.
- Still open: SBL SYS_ERR right after M0. Candidates: ath11k pre-MHI chip register writes (force_wake/ltssm/fix_l1ss/clear_dbg/
  global reset - only global reset tested, and that was while IRQs were broken), cnss2 RDDM dump to get a real crash reason.
- a26-a30: ath11k_pci with r0q_skip param (skip steps cnss2 does not do in sw_reset; test module ~/ath11k_pci-skip.ko, source restored):
  skip=15 (all) on a warm, many-times-reloaded chip -> phone HARD HUNG (no pstore dmesg; likely BAR read on a dead link; forced restart).
  skip=2 (wlaon), skip=12 (fix_l1ss + clear_dbg), skip=1 (no SoC global reset, fresh boot) -> all the same SBL SYS_ERR.
  skip=1 on a warm chip -> "SECONDARY BOOTLOADER is not a valid EE for power on" (chip still in the crashed SBL). All four writes ruled out.
  Test script ~/t.sh + ~/bhiregs.py (now skips the BAR read when the link is down or there was no SBL wait).
- a31: ath11k r0q_rddm test (~/ath11k_pci-rddm.ko, source restored): after the SBL timeout mhi_force_rddm_mode -> -5 (ee 8),
  mhi_download_rddm_image -> -5, all 9 RDDM segments empty (0 nonzero bytes). The SBL crash leaves NO crash dump.
- NOTE: everything "ruled out" before attempt 17 (ASPM/L1ss, Gen1 link speed, PARF_SYS_CTRL 0x365E, dma-coherent, rails, no-reset)
  was tested while the chip's MSIs were lost -> those results say nothing about the SBL crash and need retesting.
- a32: ASPM/L1ss all 0 + root port Gen1 (2.5 GT/s confirmed) + PARF_SYS_CTRL 0x365e, then stock ath11k -> same SBL SYS_ERR. Ruled out (post-fix).
- cnss2 AOP mbox messages (vreg_ipa, OL CPR) are sent only after QMI/fw up -> not relevant to the SBL crash.
- Suspect: attempt 14 `dma-coherent` on &pcie0 (mainline/hdk omit it). If PCIe is not really IO-coherent here, CPU-cached MHI
  context tables are never flushed -> SBL reads garbage contexts right after M0 -> SYS_ERR. Next: DT without dma-coherent (flash).

## Attempt 18 (2026-10-18 09:30): &pcie0 without dma-coherent - flashed
- DT backup kernel-config/sm8450-samsung-r0q.dts.attempt17, DTB backup out/pmos/sm8450-samsung-r0q.dtb.a17. boot.img sha256 dd826b6f...4535, readback OK.
- Live DT has no dma-coherent on pcie@1c00000. Wi-Fi (stock ath11k, fresh boot): same SBL SYS_ERR (a33). dma-coherent ruled out;
  kept removed (matches mainline/hdk).
- Upstream patch prepared: kernel-config/upstream-patches/0001-PCI-qcom-Parse-iommu-map-entries-using-iommu-cells.patch
  (driver now also accepts legacy 4-cell maps like of_check_bad_map(); checkpatch clean except the Signed-off-by the user must add;
  Fixes: tag left to fill). Root cause context: DT-only series "arm64: dts: qcom: Fix the PCIe iommu-map entries" (Jul 2026,
  M. Sadhasivam) made entries 5 cells; pcie-qcom.c config_sid still parsed 4. The legacy-detection version is compile-tested only,
  the phone runs the earlier (5-cell-only) version.

## Reference found: Galaxy Tab S8+ (SM-X800, SM8450, same WCN6855) Wi-Fi WORKS on mainline 7.2 (2026-10-18)
- github.com/aaronsb/sm-x800-linux (docs/07-input-and-wireless.md): hdk-copy DT, upstream linux-firmware amss/m3/board-2.bin
  (exact board-2 entry), no PCIe/ath11k patches. Samsung bdwlan board files crash mainline fw. Also nura wiki (ex-pmOS wiki)
  lists r0q Wi-Fi "Works" on a 6.16-rc1 kernel (Ghatto404 / Arthurus36).
- Diff vs ours: PMU node identical. Tab: S2E min 0.5 V (ours 1.012), S1C min 1.8 V (ours 1.9), no wifi@0 memory-region, mainline PHY values.
## Attempt 19 (staged): match the Tab S8+ - S2E min 0.5 V, S1C min 1.8 V, no IOVA pool/memory-region (DT backup .dts.attempt18,
  .dtb.a18) + mainline phy-qcom-qmp-pcie restored on the phone (Samsung-tuning copy kept as .ko.zst.r0q-samsung-tuning).
- (correction) the PHY module swap above first failed (phone dropped off USB); done after the phone came back:
  active phy-qcom-qmp-pcie.ko.zst = mainline (fe12b00a...), Samsung-tuned copy = .ko.zst.r0q-samsung-tuning. Tree source also restored.

## Attempt 19 result (2026-10-18 10:10): WI-FI WORKS
- boot.img sha256 103009e3...8896, readback OK. Upstream linux-firmware (Samsung amss/m3 override moved to /lib/firmware/updates/ath11k.stock-off).
- ath11k: SBL -> mission mode (EE 2, MHI M0), chip_id 0x12 chip_family 0xb board_id 0xff soc_id 0x400c1211,
  fw WLAN.HSP.1.1-03125-Q (2024-04-17). wlan0 up; nmcli scan sees 2.4 GHz (ch 7/8/13) and 5 GHz (ch 48) networks. (a34)
- Needed together: pcie-qcom BDF->SID fix (attempt 17, MSIs) + attempt 19 changes. Which attempt-19 change fixed the SBL crash is
  NOT bisected yet: S2E min 0.5 V (was 1.012), S1C min 1.8 V (was 1.9), no IOVA memory-region, mainline PHY values, upstream fw.
- ath11k_pci/ath11k unblocked in /etc/modprobe.d/s22-bringup.conf (backup .a19).
- Weak signal: ~6 m from a Wi-Fi 6/7 router, 2.4 GHz "HOME-2G" only -74/-75 dBm (expected -40..-55); 5 GHz "HOME-5G" (ch 48) seen once at 20 %,
  a neighbour's 5500 MHz AP at -87 dBm. Regdomain was 00 -> set AU (iw reg set AU; persisted /etc/modprobe.d/s22-wifi-regdom.conf;
  iw + wireless-regdb installed).
- a35: Samsung board file (bdwlang.elf: board_id 0xff + soc_id&0x11 GF, no EFS antenna file) as board.bin with upstream fw: loads, no crash,
  but signal identical (-74 dBm) -> calibration not the cause. Reverted to upstream board-2.bin.
- Real Wi-Fi MAC is in EFS /wifi/.mac.info (todo: set at boot like BT).
- Open hypothesis: Wi-Fi antenna path shared with cellular RF front end, switched by the (not running) modem.

## Attempt 20 (staged): modem (MPSS) bring-up
- Mainline: remoteproc_mpss (qcom,sm8450-mpss-pas, auto_boot=false), mpss_mem 0x8bc00000+0x13200000 = stock; rmtfs_mem 0x9fd00000 (no stock overlap).
  No IPA for sm8450 in mainline (mobile data not possible yet).
- Firmware: phone's own modem partition (sda24, mounted ro) image/modem.mdt+bXX, modemr.jsn, modem_pr -> /lib/firmware/qcom/sm8450/samsung/r0q/.
- EFS: rmtfs in FILE mode from verified backups (/var/lib/rmtfs/modem_fs1/fs2/fsg/fsc) + rmtfs_avoid_writing=true -> real
  modemst1/2/fsg/fsc partitions are never opened for writing. Installed rmtfs, tqftpserv (+qrtr, iw). PD mapper is in-kernel (QCOM_PD_MAPPER=m).
- Blocklist: qcom_q6v5_pas + qcom_pil_info unblocked (backup s22-bringup.conf.a20). DT: &remoteproc_mpss okay + firmware-name.
  DT backup .dts.attempt20 (= new), previous DTB .dtb.a19. apk install triggered pmOS mkinitfs/boot-deploy (only /boot files, harmless here).

## Attempt 20 result (2026-10-18 10:30): MODEM BOOTS
- boot.img sha256 3b906126...a78d, readback OK. remoteproc0 "modem" -> rc-service tqftpserv + rmtfs (-s -o /var/lib/rmtfs -r, verified
  in /proc cmdline, no block devices open) -> "remote processor modem is now up".
- GLINK channels: IPCRTR, DATA1-11, DS, SSM_RTR_MODEM_APPS, modem_apps... qrtr-lookup: 52 modem services (DMS, NAS, UIM, WDS, Voice, PBM, CAT, AT...).
- qmicli -d qrtr://0 (pkg qmi-utils): QUALCOMM INCORPORATED, model 241, MPSS.DE.2.0-00822.3-WAIPIO_GEN_PACK (Apr 2025), mode online.
  UIM card status (both slots): error no-atr-received -> SIM not answering (or none inserted; ask user).
- ModemManager 1.25.95 (qcom-soc plugin, QRTR) lists no modem yet; it gets D-Bus-activated by Plasma so a separate --debug run can't
  take the bus name. To investigate.
- Wi-Fi signal with modem up: unchanged (-73 dBm 2.4 GHz; "HOME-5G" -88 dBm now seen every scan with regdom AU).
- Packages added: rmtfs, tqftpserv, qrtr, iw, wireless-regdb, qmi-utils.

## Keyboard / RAM / 120 Hz (2026-10-18 ~10:45)
- On-screen keyboard: kwinrc [Wayland] InputMethod was IBus (no OSK) -> org.kde.plasma.keyboard.desktop (backup ~/.config/kwinrc.bak-osk).
  Starts with the session after reboot; hide key works, no auto-hide yet (polish).
- RAM: 8205312K physical, ~6.2 GB available. Our DT reserves 1450 MiB no-map vs stock 707 MiB fixed (+1278 MiB reusable CMA).
  Biggest extra: mainline sm8450.dtsi mte_mem 0xc0000000+512 MiB, absent in stock (normal RAM on Android) ->
  attempt 21 (STAGED, not flashed): /delete-node/ &mte_mem; DT backup .dts.attempt21, previous DTB .dtb.a20.
  (ea000000/ed900000 regions lie in the stock memory-map hole 0xea000000-0xf1c00000, cost nothing.)
- 120 Hz: panel driver has only fhd60hs (120 Hz timing with VFP 3441). Samsung ss_vrr(): FREQ 0x60 (0x00 120HS / 0x08 60HS / 0x18 60NS),
  OSC 0xF2 [1] (01 / 00 at 60HS,48HS), GLUT offset 0x93 (off at 120HS), TE_SEL 0xB9 per-mode table. Module-only work (no flash).
## Attempt 21 result (2026-10-18 10:46): +512 MiB RAM
- boot.img sha256 f0f68164...fd95, readback OK. MemTotal 6267 -> 6778 MiB ("Memory: 6868496K/8205312K available"). Wi-Fi + OSK fine.

## 120 Hz (2026-10-18 10:52) - module only, no flash
- Stock panel node ss_dsi_panel_S6E3FAC_AMB606AW01_FHD: fhd120hs h 92/92/92, v fp 467 bp 84 pw 84 (vtotal 2975; fhd60hs = 5950, same pclk).
  Stock fhd120hs vs fhd60hs on-commands differ only in: 60 08->00 (x2), 94 01->18 (after b0 00 0e 94), f2 00->01.
- panel-samsung-s6e3fac.c: module param refresh=120 (default) | 60 picks mode + those bytes (60 Hz-only source kept as .c.60hz-only;
  phone module backup .ko.zst.60hz-only). After reboot: refresh=120, KWin DSI-1 1080x2340@120, panel init + display on OK (0x9d).
- Fallback if needed: echo "options panel_samsung_s6e3fac refresh=60" > /etc/modprobe.d/s22-panel.conf, reboot.

## Wi-Fi connect fails (2026-10-18 ~11:10)
- Real MAC set: NM conf.d/s22-wifi-mac.conf (cloned-mac 48:BC:E1:XX:XX:XX, no scan randomisation) + udev rule 90-s22-wlan-mac.rules.
- Connecting to "HOME-2G" (2.4 GHz, -74 dBm): "send auth (try 1-3/3) ... authentication timed out" every time, with WPA3/SAE and with
  WPA2 (profile left on wpa-psk). txpower 15 dBm, AU limit 20 dBm. L7E antenna-switch rail on at 2.8 V.
  => router does not hear the phone: RX ~25 dB low AND TX not received -> RF/antenna path problem, not config.
- a36: Samsung bdwlang.elf again + connect attempt: same timeout; no board-load debug output (ATH11K_DEBUG probably off) -> inconclusive.
- USB: user still replugs; s22-usb-reconnect.start now logs to /var/log/s22-usb.log (test next reboot WITHOUT replug).

## Attempt 22 (2026-10-18 11:05): nftables kernel - flashed
- .config: NF_TABLES=m + inet/ipv4/ipv6/netdev, NFT_CT/REJECT/NAT/MASQ/LOG/LIMIT/FIB/COMPAT, NF_NAT (backup .config.before-nft).
  Changes struct net layout -> full Image + all modules rebuilt (kernel #19). Phone modules: old dir kept as /lib/modules/7.2.11-dirty.a21.
  boot.img sha256 3b0daece...ea45, readback OK. Old kernel: out/pmos/vmlinuz.a21-build18.
- Result: nftables service started, table inet filter loaded; "kernel lacks nftables" boot message gone.
- hwclock removed from boot runlevel (RTC read-only; s22-clock handles time) -> "System Clock" boot failure gone.
- polkit shows [failed] in rc-status but polkitd runs from boot (slow start); restart -> started.
- USB: s22-usb-reconnect ran 3 soft reconnects at boot, none worked (needs real attach event). Proper fix: MAX77705 Type-C /
  usb-role-switch (Tab S8+ port has max77705-typec*.patch + dwc3-host-power-lost.patch). Script left in place (harmless).

## Attempt 23 (staged): MAX77705 Type-C (CCIC) for USB attach without replug
- Applied 12 Galaxy Tab S8+ patches (kernel-config/upstream-patches/tabs8/, patch -b .orig backups): max17042 i2c id, charger otg
  regulator/battery-info/otg-shutdown/aicl x2/review-fixes, max77705-typec (+source-attach, prefer-voltage, review-fixes),
  dwc3-host-power-lost. Skipped max77705-mfd-pass2 (= our rev 0x2 patch).
- typec/Makefile: module renamed max77705_typec.ko (name clash with mfd max77705.ko, which is =m here; built-in on the Tab).
- CONFIG_TYPEC_MAX77705=m. CCIC answers at i2c-0 0x25 (regs 00-02: 1a 6e 72).
- DT: usb_1 dr_mode otg + role-switch-default-mode peripheral (initramfs gadget still works before the CCIC module loads),
  usb_1_dwc3_hs <-> typec@25 connector port@0, charger usb-otg-vbus regulator, dt-bindings/usb/pd.h. DT backup .dts.attempt23.
- Kernel #20 (Image: dwc3 patch). Backups: out/pmos/vmlinuz.a22-build19, dtb.a22; modules tarball modules-a23.tgz on phone /var/tmp.
## Attempt 23 result (2026-10-18 11:25): USB COMES UP WITHOUT REPLUG
- boot.img sha256 615effd8...e691, readback OK, kernel #20. Old modules kept as /lib/modules/7.2.11-dirty.a22.
- "max77705-usbc 0-0025: CCIC hw 0x1a fw 6E.00"; typec port0 data_role [device], power_role [sink], partner (PC) detected;
  usb_role device, UDC configured, SSH up ~25 s after boot with no replug. Soft-reconnect script retired (~/s22-usb-reconnect.start.disabled).

## Audio - IN PROGRESS (paused 2026-10-18 ~11:40, nothing flashed)
- HW (stock DT): 2x Cirrus CS35L41 on i2c4 (0x990000): @40 bottom speaker "cs35l40-bot" reset tlmm183, @41 earpiece "cs35l40-rcv"
  reset tlmm18, shared IRQ tlmm19 (gpio2-src-select 2 = open-drain INTB), VA/VP dummy (always-on), boost-peak 4100 mA (Samsung
  sets no ind/cap -> plan: boost-ind-nanohenry 1000, boost-cap-microfarad 15 = Cirrus reference; start at LOW volume).
  Primary MI2S gpio126 sck / 129 ws / 127 data0 (amp TX) / 128 data1 (playback) = SAME as Galaxy Tab S8+ -> reuse its SD1 topology.
- Memory: stock adsp 0x84500000+0x3b00000, video 0x83e00000+0x700000, cdsp_secure_heap 0x80c00000+0x600000 (dtsi: 70 MiB heap,
  adsp 0x85e00000). Plan: override &adsp_mem/&video_mem/&cdsp_secure_heap like the Tab, delete our samsung-adsp@84500000 (+64 MiB RAM).
- Done in tree (not built): CONFIG_SND_SOC_CS35L41_I2C=m; applied Tab sc8280xp-mi2s-codec-fmt-sysclk.patch (-b backup).
  7.2 already has the 3 Sept-2025 MI2S fixes. Fetched Tab tplg/UCM to scratch (copy into project when resuming).
- TODO: DT (remoteproc_adsp fw qcom/sm8450/samsung/r0q/adsp.mdt from recon/firmware_mnt/image, pri_mi2s_active + cs35l41 pinctrl,
  &i2c4 amps, &sound qcom,sm8450-sndcard PRIMARY_MI2S_RX), unblock LPASS modules on phone, tplg as Samsung-Galaxy-S22-tplg.bin,
  UCM (CS35L41 controls: "Digital PCM Volume", "Analog PCM Volume"...), then mics (stock cdc-dmic pins, L12C like the Tab).

## USB drops after idle = system suspend (2026-10-18 16:16)
- Phone kernel log: "PM: suspend entry (deep)" at 921 s and 2636 s, resume ~2 s later with "dwc3-qcom a600000.usb: port-1 HS-PHY not in L2";
  USB never re-enumerates until a replug. Phone itself stays up (uptime 4:50).
- Workaround: /etc/elogind/sleep.conf.d/90-s22-no-suspend.conf (AllowSuspend=no etc.; CanSuspend now "no") + powerdevilrc
  AutoSuspendAction=0 for AC/Battery/LowBattery. Screen blanking still allowed. Remove once suspend/resume (USB PHY) works.

## Attempt 24 (staged): audio stage 1 - ADSP + 2x CS35L41 speakers (DTB only, kernel #20 unchanged)
- DT: &adsp_mem 0x84500000+0x3b00000, &video_mem 0x83e00000+0x700000, &cdsp_secure_heap 0x80c00000+0x600000 (= stock),
  samsung-adsp carve-out removed (frees 0x81200000-0x83e00000, 44 MiB). remoteproc_adsp fw qcom/sm8450/samsung/r0q/adsp.mdt.
  tlmm pri_mi2s_active (126/129/127/128) + cs35l41_default (18/19/183). i2c4 @1 MHz: cs35l41 @41 "Earpiece" reset 18,
  @40 "Speaker" reset 183, IRQ tlmm19 low, boost 1000 nH / 15 uF / 4100 mA, asp-sdout-hiz 3, gpio2 INTB.
  &sound qcom,sm8450-sndcard model Samsung-Galaxy-S22, PRIMARY_MI2S_RX -> both amps. DT backup .dts.attempt23-final, DTB .dtb.a23.
- Phone: adsp.mdt+b* (25 files) -> /lib/firmware/qcom/sm8450/samsung/r0q/; Tab S8+ SD1 topology as qcom/sm8450/Samsung-Galaxy-S22-tplg.bin;
  modules cs_dsp, wm-adsp, cs35l41(-lib,-i2c), sc8280xp (patched; old .pre-a24). LPASS lpi/macros still blocked (speakers first).
- USB-C to USB-C cable: PC does not see the phone (C-to-A works). To test: plug C-C 30 s, back to C-A, read typec role log.

## Attempt 24 result (2026-10-18 ~16:40): SPEAKER AUDIO WORKS (bottom speaker)
- boot.img sha256 79729cd4...cfc2, readback OK. ADSP up (Samsung fw), both CS35L41 probe (35a40 rev A0), card0 "Samsung-Galaxy-S22",
  RAM 6822 MiB (+44 MiB from the stock ADSP/video/cdsp_secure layout).
- Problem 1: "Enable(1) failed: -110" -> raw status3 PLL_UNLOCK, PLL_CLK_CTRL freq index 0: sc8280xp only called
  snd_soc_dai_set_sysclk (FS monitor); CS35L41 PLL ref comes from snd_soc_component_set_sysclk(CS35L41_CLKID_SCLK=0).
  Fix: also call component sysclk with 1.536 MHz (as Intel sof_cirrus_common does) -> PLL_CLK_CTRL 0x370, PLL locks.
  Patch: kernel-config/upstream-patches/local-sc8280xp-cs35l41-pll-reference.patch.
- Problem 2: still -110: q6apm BE starts the I2S graph at trigger, after DAPM PRE_PMU powered the amps (no BCLK yet).
  Fix: start MI2S ports at the end of q6apm_lpass_dai_prepare (is_port_started makes trigger a no-op).
  Patch: local-q6apm-mi2s-start-bclk-at-prepare.patch. Backups on phone: *.ko.zst.pre-pll / .pre-bclk.
- Then: amps on (PWR_CTRL1=1, PLL locked) and user hears pink noise from the BOTTOM speaker. Default analog gain 0 (min) ->
  test levels: Analog PCM Volume 10, Speaker Digital 785 (-4 dB), Earpiece Digital 721. No speaker protection fw: keep levels moderate.
- Route: amixer cset "PRIMARY_MI2S_RX Audio Mixer MultiMedia1" 1; test: speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE.
- User heard pink noise and a beep from the BOTTOM speaker.
- UCM (phone-config/ucm2 -> /usr/share/alsa/ucm2): conf.d/sm8450/Samsung-Galaxy-S22.conf (BootSequence: PCM Source ASP, Analog 10,
  Speaker Digital 785, Earpiece Digital 721) + Qualcomm/sm8450/samsung-r0q/HiFi.conf (verb routes MultiMedia1 -> PRIMARY_MI2S_RX,
  device Speaker = hw:0,0, software volume). PulseAudio: default sink alsa_output.platform-sound.HiFi__Speaker__sink; paplay works.
- TODO audio: earpiece as 2nd stereo channel (both amps take ASP RX slot 0 now), mics (VA macro DMICs + LPASS modules, L12C like the Tab),
  speaker-protection firmware (cs35l41 wmfw/tuning from vendor partition) before raising levels, calls (needs modem audio).
- TOP loudspeaker (earpiece amp @41) confirmed by user: tone with bottom muted, amp PWR_CTRL1=1, PLL locked. Both amps now Digital 785 (UCM updated). Both take ASP RX slot 0 (mono); stereo split (bottom = right, stock right-channel-amp) TODO.

## Attempt 25 (2026-10-18 ~16:45): stereo speakers
- cs35l41.c: new optional DT bool "cirrus,right-channel-amp" (Samsung's own property name) -> RX1 slot 1 via cs35l41_set_channels()
  in cs35l41_set_pdata (regmap-cached, survives runtime PM). Patch local-cs35l41-right-channel-amp.patch; phone module .pre-rightchan.
- DT: amplifier@40 (bottom) gets cirrus,right-channel-amp. boot.img sha256 f179fbfd...ea2d, readback OK. DTB backup .dtb.a24.
- Result: SP_FRAME_RX_SLOT bottom 0x101 (right), top 0x100 (left); speaker-test -t wav played Front Left / Front Right.

## Attempt 26 (2026-10-18 ~16:55): microphones
- DT: &lpass_tlmm dmic45 state (gpio12/13 dmic3), &vamacro okay (dmic01/23/45 pinctrl, vdd-micb L12C, 2.4 MHz), &sound
  va-capture-dai-link VA_CODEC_DMA_TX_0 -> vamacro. Phone: pinctrl_sm8450_lpass_lpi unblocked (backup s22-bringup.conf.a26);
  rx/tx/wsa macros still blocked. boot.img sha256 909ef862...0128, readback OK. Capture PCM 00-02 (MultiMedia3, stereo only).
- Level scan (stereo pairs, user talking): DMIC0 0, DMIC1 peak 155, DMIC2=DMIC3 peak 1822, DMIC4=DMIC5 peak 2326 -> two working mics
  (pins 8/9 and 12/13), DMIC1 maybe a weak third. 4 s record + playback on speakers works (DMIC4 peak 962 @ VA_DEC 80%).
- UCM Mic device (MUX0 DMIC4, MUX1 DMIC2, DEC0/1 85%, hw:0,2); PulseAudio default source alsa_input.platform-sound.HiFi__Mic__source.
- CORRECTION: first level scan had no sound source (user silent). Quiet vs noise test: DMIC1 rms 50->2092, DMIC4 107->3045 (both clip on a clap) = real mics. UCM Mic now MUX0 DMIC1, MUX1 DMIC4.
- Wi-Fi: rails ruled out live (wifirails.ko: S2E 0.5 -> 1.012 V, S1C -> 1.904 V, S12B 1.352 V): RSSI unchanged (-76/-88 dBm), auth still
  times out. Remaining lead: confirm ath11k really loads Samsung bdwlang.elf (enable ATH11K_DEBUG) - RX weak + TX unheard = FEM/antenna switch off.
- Speakers raised (user: too quiet): Analog PCM Volume 10 -> 14, Digital 785 -> 817 (0 dB) on both amps, UCM BootSequence updated.
  Still no speaker protection firmware: avoid long max-volume playback.

## KRecorder, mic gain, volume up (2026-10-18 ~17:05)
- KRecorder (apk krecorder) recorded 0 s: Qt Multimedia FFmpeg backend opens the PulseAudio mic but never receives a frame (129-byte
  Ogg/Opus header only). PulseAudio itself fine (parecord at 200/20/5 ms latency). Fix: qt6-qtmultimedia-gstreamer + gst-plugins-base/good,
  launcher override ~/.local/share/applications/org.kde.krecorder.desktop Exec=env QT_MEDIA_BACKEND=gstreamer krecorder -> user: records.
- Mic gain: VA_DEC0/1 Volume 105 (+21 dB) -> 112 (+28 dB) in UCM (user: a little low).
- Attempt 27 (staged): volume up never wired - DT had no gpio-keys (power/vol-down are PON pwrkey/resin). Added gpio-keys
  KEY_VOLUMEUP on pm8350 gpio6 active low + vol_up_default (normal, power-source 1, pull-up, input) = stock / Galaxy Tab S8+.
  DT backup .dts.attempt27, previous DTB .dtb.a26. (Tab notes: works 3 of 4 boots, rare PMIC latch glitch.)
- Attempt 27 result: boot.img sha256 bbb02048...b177, readback OK. gpio-keys event2; pm8350 gpio6 in/high/pull-up 30uA, "Volume Up"
  irq (spmi-gpio 5) counted 13 edges on a press; user confirms VOLUME UP WORKS in Plasma.
- 6.16 comparison: Arthurus36 "PostMarketOS-Modified-Kernel-r0q" r0q.dts is only a decompiled stock DT; the wiki's mainline 6.16
  (Ghatto404) tree is not public. Kernel 7.2 is not the Wi-Fi problem (Tab S8+ Wi-Fi works on 7.2; the 7.x pcie-qcom bug is fixed).

## Wi-Fi A/B/C firmware x board-file test (2026-10-18 ~17:25), log a37
- A upstream fw WLAN.HSP.1.1-03125 + generic board-2.bin; B + Samsung bdwlang.elf; C SAMSUNG fw WLAN.HSP.2.0.c1-00441 (2025-04-28) +
  bdwlang.elf (= Android's pair). Samsung fw now BOOTS under ath11k (attempt-19 DT fixed the old SBL crash).
  All three: HOME-2G 2.4 GHz -74 dBm, "authentication timed out". Firmware/BDF ruled out.
- ATH11K_DEBUG=y did not take effect (M= build without syncconfig -> autoconf.h stale); rebuild after `make prepare` if needed.
- Bluetooth (same chip, same antenna switch): btmgmt find 15 s -> only 1 device at -92 dBm => shared 2.4 GHz antenna path ~25 dB
  attenuated for BT AND Wi-Fi. Remaining causes: RF switch/tuner on the shared antenna not set (modem/GRFC side?) or hardware
  (antenna flex/cable). Asked user: repairs? Wi-Fi/BT fine under Android?

## WI-FI CONNECTS (2026-10-18 ~17:45) - "HOME-5G", 960 Mbit/s HE-MCS9 2x2, internet OK
Root cause chain (all needed together, matching what Android/cnss2 does):
1. Board file: the generic board-2.bin (7.2 MB) was used in EVERY earlier "Samsung BDF" test - an EMPTY override board-2.bin is
   skipped by the firmware loader. Fix: a non-empty dummy /lib/firmware/updates/ath11k/WCN6855/hw2.1/board-2.bin ("NOT-A-BOARD-2-FILE")
   so ath11k falls back to board.bin = Samsung bdwlang.elf (board_id 0xff + soc_id GF bit). RX: 2.4 GHz -74 -> -35 dBm, 5 GHz -88 -> -51.
2. Samsung firmware: upstream fw + Samsung BDF crashes (no wlan0); Samsung amss.bin/m3.bin (WLAN.HSP.2.0.c1-00441, 2025-04-28) + BDF boots.
   Plus Samsung regdb.bin (else regdb came from the generic board-2.bin).
3. TX still failed (mgmt tx compl status 3 = no ACK, every chain): stock DT has qcom,wlan-cbc-enabled -> ath11k core.c WCN6855 hw2.1:
   coldboot_cal_mm = true + cbcal_restart_fw = true (calibrate in mode 7, restart fw, then mission). Patch:
   kernel-config/upstream-patches/local-ath11k-wcn6855-hw21-coldboot-cal.patch (ath11k built with ATH11K_DEBUG=y as well).
- Then 5 GHz "HOME-5G" connected (user typed the password via ssh -t nmcli --ask): 192.168.x.x, signal -59 dBm, TX 960.7 Mbit/s
  80 MHz HE-NSS 2, ping 1.1.1.1 over wlan0 4/4. 2.4 GHz "HOME-2G" still auth-timeout (not retested after CBC with fresh profile) - TODO.
- Not bisected: which of CBC / regdb was strictly needed (all kept, = Android behaviour). Logs a37-a42.
- Lessons: verify firmware loading with ath11k debug (needs `make syncconfig` before M= builds); empty firmware files are ignored.

## Wi-Fi at boot + apps (2026-10-18 ~17:50)
- After reboot: "Coldboot Calibration timed out" (60 s) when ath11k loads at early boot -> no wlan0. modprobe -r of that stuck
  driver FROZE the phone (forced restart). Fix: /etc/modprobe.d/s22-wifi-late.conf blacklist ath11k_pci +
  /etc/local.d/s22-wifi.start (sleep 20; modprobe ath11k_pci; iw reg set AU; log /var/log/s22-wifi.log). Reboot test: boot ~35 s,
  wlan0 up, autoconnect "HOME-5G" 192.168.x.x, keyboard running. NEVER rmmod ath11k while calibration is stuck.
- Keyboard: kwinrc InputMethod had been rewritten to IBus (ibus installed); set again + system default /etc/xdg/kwinrc; works after reboot.
- Installed: qmlkonsole (terminal), chromium (no Google Chrome for aarch64 Linux), discover + discover-backend-flatpak + flatpak,
  Flathub remote (system), xdg-user-dirs. Test files moved to ~/s22-tests.
- Google Chrome: Flathub com.google.Chrome aarch64 155.0.8059.84 (wraps Google's official ARM64 build; the .deb/.rpm are glibc, pmOS is musl). Chromium also installed.

## 2026-10-18 attempt 28
- MAX77705 INT pin power-source <1> (stock). IRQ storm (200001) gone, hotplug works.
- MTP gadget added (umtprd). PC sees "Galaxy S22".

## 2026-10-20 attempt 29: CDSP up
- uniLoader DTB relocation (Tab S8+ 0001-dtb-relocate.patch, applied in uniLoader/upstream; CONFIG_DTB_ENTRY=0xb6000000). The DTB had been left at the end of the uniLoader image (0x8b7c2000) inside cdsp_mem.
- DT: full 32 MiB cdsp_mem restored (2 MiB pieces removed), &remoteproc_cdsp okay with Samsung cdsp.mdt (+18 files from recon/firmware_mnt/image).
- Result: cdsp running, /dev/fastrpc-cdsp + -secure, 0 failed reservations, RAM 6820 MiB. Boot image sha256 69d51ce6...
- Vulkan: mesa-vulkan-freedreno installed, Turnip finds renderD128; llama.cpp build-vk built on phone (~/bench/llama.cpp/build-vk).
- Lesson: build-uniloader.sh recreates ~/s22-kernel/uniloader from uniLoader/upstream on every run; patch upstream copy.

## 2026-10-20 attempt 30: crypto node disabled -> memory bus unpinned (REVERTED to 29)
- Cause of the always-max memory bus found: interconnect sync_state() "pending due to 1dfa000.crypto". qcrypto is blacklisted
  (must stay blocked), so the QCE node never binds and the interconnect never leaves its boot-time "everything at max" state.
  ebi read 2147483647 (INT_MAX) at idle. Likely a large part of the ~8 h idle battery life.
- Attempt 30 set &crypto status = "disabled": sync_state ran, ebi dropped to 240000/700000 (USB vote only).
- BUT nothing else votes for DDR: CPU 0.5B tg 34 -> 4.6 t/s, GPU 3B tg 8.5 -> 0.16 t/s. sm8450.dtsi has no CPU/LLCC bwmon
  nodes (QCOM_ICC_BWMON=m is built) and the gpu node has no interconnects; a730 GMU HFI bandwidth voting is not used
  (this is why GX_BW_PERF_VOTE timeouts never hurt: the bus was pinned).
- Reverted to attempt 29 (boot.img 69d51ce6...), DTS + DTB restored to a29. Attempt 30 kept as kernel-config/*.dts.attempt30, dtb.a30.
- Proper fix (TODO): add CPU + LLCC bwmon nodes and GPU interconnects/opp-peak-kBps from the stock DT, THEN disable &crypto.

## 2026-10-20 NPU session reliability (attempt 29, no flash)
- Calculator-only runs: 0/5 when libcdsprpc used /dev/fastrpc-cdsp-secure (it prefers the secure node). INIT_CREATE ok,
  first INVOKE (skel open) fails 0x80000406 (DSP AEE_EUNABLETOLOAD).
- Secure node made root-only (udev 60-fastrpc.rules) -> lib uses /dev/fastrpc-cdsp like Android unsigned apps: 3/5.
- cdsprpcd running: 4/6 pass; stopped: 1/6. Longer gaps between runs do not help (not a teardown delay).
- Remaining failures: skel load on first call, looks like a start-up race. Apps should retry remote_handle_open.

## 2026-10-20 attempts 31 + 32: memory bandwidth voting, bus no longer pinned at max
- 31: CPU bwmon pmu@190b6400 (qcom,sdm845-bwmon, IRQ 581, LLCC steps 4.58-16.27 GB/s) + DDR bwmon pmu@19091000
  (qcom,sc7280-llcc-bwmon, IRQ 81, DDR steps 2.09-12.19 GB/s) from stock qcom,bwmon-llcc/-ddr + ddr/llcc freq tables;
  GPU interconnects "gfx-mem" + opp-peak-kBps from stock gpu-pwrlevels bus-freq -> bus-table-ddr. icc_bwmon loads,
  both pmu bind, under CPU load they vote 12191000 / 16265000, GPU votes 1761718 at 220 MHz. Boot img dee37735...
- 32: + &crypto status disabled. Interconnect sync_state now runs; ebi at idle = 2086000 (lowest DDR step) instead of INT_MAX.
  CPU 0.5B tg 32.1 t/s (pp128 110.9), GPU 3B tg 8.75 (pp128 45.9): same as the pinned bus. Wi-Fi/audio/NPU OK.
  Boot img efc8daab... Remaining: gcc/gpucc sync_state pending on 3d6a000.gmu (unused clocks kept on).

## 2026-10-20 brightness floor
- Panel shows black below DBV ~3-6. User picked 6 by eye. panel-samsung-s6e3fac maps the slider onto [6, 2047] (blank still 0).
  Module-only rebuild, installed with .pre-minbl backup. Patch: kernel-config/local-s6e3fac-min-brightness.patch

## 2026-10-20 attempt 33: VIBRATION WORKS (CS40L26 ROM effects, no driver)
- Haptics IC is Cirrus CS40L26 (stock cs40l26a@40 on i2c@a84000 = QUP SE9, input_cs40l26_i2c on Android), reset tlmm 180,
  IRQ pm8350 gpio9. PM8350B hv-haptics exists but is unused. Firmware cs40l26.wmfw + *.bin in recon/vendor_firmware.
- DT: &qupv3_id_1 okay, &i2c9 okay @400 kHz (gpio32/33), reset held high via pinctrl. Bus appears as i2c-2.
- DEVID 0x0040A260 (first access NACKs: chip wakes from hibernation), REVID 0xA1.
- Mailbox 0x13020: 0x02000003 prevent hibernation, then 0x01800000|n plays ROM effect n (39 effects); 0x05000000 stops.
  MBOX_2 reads 0x02000002 (AWAKE). User felt it. Tool: tools/s22-vibrate.sh (/usr/local/bin/s22-vibrate).
- No mainline cs40l26 driver (only cs40l50). Next: small input FF driver using ROM effects, later full driver + firmware.

## 2026-10-20 attempt 34: vibration driver, works in Plasma
- New out-of-tree driver kernel/cs40l26-rom-vibra (input FF_RUMBLE via ff-memless, I2C work in a workqueue): wakes the
  CS40L26 (prevent hibernation), plays ROM effect `rom_effect`, replays it every `retrigger_ms` (40) so a rumble is a
  continuous buzz, pause + allow hibernation on stop. Installed in /lib/modules/$(uname -r)/extra.
- ROM effect strength by feel: 0 low, 3 medium, 6 high. Default 3 (/etc/modprobe.d/s22-haptics.conf).
- DT: haptics@40 compatible "cirrus,cs40l26a" under &i2c9 -> binds at boot. Boot img 2c8f31ff...
- feedbackd only uses whitelisted names: /etc/udev/rules.d/72-s22-feedbackd.rules sets FEEDBACKD_TYPE=vibra.
  fbcli button-pressed / message-new-instant felt by user. Plasma session bus is /tmp/dbus-* (take it from plasmashell).

## 2026-10-20 attempt 35: cameras phase 1, CAMSS up (kernel #21)
- Tab S8+ patches applied cleanly to 7.2.11: sm8450-camss.patch (CAMSS_8450: csiphy 2.1.0, csid680, vfe680 ops, camss node in
  sm8450.dtsi, binding), camcc-sm8450-shared-rcg.patch + camcc-sm8450-retain-ff.patch (second camera session works).
  Copies in kernel-config/upstream-patches/tabs8/. Kernel rebuild: vmlinuz.a35-build21 (old: vmlinuz.a34-build20),
  modules installed on the phone (old set: /lib/modules/7.2.11-dirty.a34; extra/ kept).
- DT: &camss (vdda-phy L5B 0.88 V, vdda-pll L6B 1.2 V) + &cci0 + &cci1, no sensor ports yet. Boot img daa70a69...
- media-ctl: qcom-camss, 6 CSIPHY, 5 CSID, VFE0-2 x3 lines, VFE3-4 x4, 17 video nodes (same as the Tab). 4 CCI buses.
  NOTE: I2C numbering changed: CCI = i2c-0,2,3,4; i2c@990000 (amps) i2c-1, typec i2c-5, CS40L26 i2c-6.
- Stock sensors: main S5KGN3 csiphy1/cci0/mclk2/reset0; ultrawide IMX374 csiphy2/cci0/mclk1/reset2;
  tele S5K3K1 csiphy0/cci1/mclk3/reset3; front S5K3K1 csiphy4/cci1/mclk4/reset1. All 19.2 MHz MCLK.
- Next (phase 2): S5K3K1 driver (front + tele) from the stock CamX sensor settings on the vendor partition.

## 2026-10-20 cameras phase 2 step 1: stock CamX sensor files extracted
- CORRECTED sensor map (vendor sensormodule slot = stock cam-sensor cell-index):
  0 main S5KGN3 (csiphy1/cci0/mclk2), 1+12 FRONT Sony IMX374 (csiphy4/cci1/mclk4), 2 ULTRAWIDE Sony IMX563
  (csiphy2/cci0/mclk1), 3 TELE S5K3K1 (csiphy0/cci1/mclk3). Four different sensors.
- How: lpunpack -p vendor /dev/sda29 (super, read only) -> /var/tmp/lp/vendor.img (F2FS, Samsung file compression).
  Kernel had no F2FS: built F2FS_FS=m + F2FS_FS_COMPRESSION (LZO/LZ4/ZSTD, not LZ4HC: LZ4_compress_HC not in vmlinux
  symvers) as an out-of-tree install in /lib/modules/7.2.11-dirty/extra/f2fs.ko. .config now has F2FS=m (config backup
  kernel-config/config-build21.pre-f2fs). M= F2FS rebuild needed `make M=fs/f2fs clean` after config changes.
- Copied vendor/lib64/camera (41 MB) to recon/vendor_camera (proprietary: NOT in the git repo).
  sensormodule.*.bin = "QTI Chromatix Header / Parameter Parser V3.4.0": sensorDriverData, powerSetting, resolutionData,
  exposure/gain register addrs, streamConfiguration, ~1133 regSetting entries. Needs a decoder (step 2).

## 2026-10-20 cameras phase 2 step 2: CamX sensormodule decoder (tools/camx-chromatix-dump.py)
- Format ("QTI Chromatix Header", Parameter Parser V3.4.0): sections table at 0xA4 {count, (id, offset, size)...};
  section 0 = 56-byte records {u32 id, char name[32], u32 kind, u32 a, u32 b, u32 data_off, u32 data_size};
  section 1 = de-duplicated data blob. regSetting blob = 40-byte entries
  {0, slaveAddr_rec, registerAddr, addrType, registerData_rec, dataType, op, 0, 1, delayUs_rec}; values via record ids.
- IMX374 (front): exposure 0x224, again 0x216, dgain 0x20E; initSettings 341 writes; 7 modes x 120/121 writes, RAW10:
  2944x2208@30 HDR3, 3216x2208@30 HDR3, 3216x1808@30 HDR3, 1988x1120@120, 912x684@120, 1616x912@30, 1472x1104@30.
  Generated C tables: recon/vendor_camera/imx374-tables.h (1183 writes; private, not in git). First 3 writes 0x0112/0x0113=0x0A
  (RAW10), 0x0114=0x03 (4 lanes): standard Sony layout, confirms the decode.
- Next (step 3): imx374 driver (v4l2 subdev, cci regmap) + front camera DT (csiphy4, cci1, mclk4, reset1, rails).

## 2026-10-20 attempt 36: S2MPB02 camera PMIC under Linux control
- Camera rails come from a Samsung S2MPB02 (stock s2mpb02_pmic@59) on an i2c-gpio bus, tlmm 175 SDA / 176 SCL (now i2c-0).
  No mainline driver: new out-of-tree s2mpb02-regulator (LDO1-18; LDOn ctrl = 0x1d + n, bit7 enable, bits 6:0 vsel,
  600 mV + 12.5 mV (LDO1-5) / 25 mV (LDO6-18); from Samsung's GPL driver). Boot img 6bea1b68...
- Front camera (IMX374, stock cam-sensor1): vdig LDO5 1.05 V, vio LDO9 1.8 V, vana LDO15 2.8 V, MCLK4 gpio104, reset gpio24,
  CCI1, CSIPHY4. CamX: I2C 0x20 (7-bit 0x10), ID reg 0x0016 = 0x0374. Power-up: vana, vio, vdig (1 ms each), MCLK 19.2 MHz,
  reset release, 10 ms.
- Verified: driver set LDO5 from 1.2 V to 1.05 V; registers read 0x22=0x24, 0x26=0x30, 0x2c=0x58 (correct, all off).
- Next (attempt 37): imx374 sensor driver + front camera DT node (power, MCLK, reset, CSIPHY4 endpoint), read chip ID.

## 2026-10-20 attempts 37-39: front IMX374 not answering yet (open)
- imx374 driver (out-of-tree, 1616x912 RAW10 @ 30, 4 lanes, link 1046.4 MHz, pixel rate 837.12 M) + DT camera@10.
- 37 on cci1_i2c1: -ENXIO. Mainline pin map is one master off from stock (stock cci0 m1=110/111, cci1 m0=112/113,
  cci1 m1=114/115; mainline cci0 m0=110/111, m1=112/113, cci1 m0=114/115, m1=208/209). 38 added
  power-domains TITAN_TOP_GDSC (MCLK4 lives in it). 39 moved to cci1_i2c0 (gpio114/115): now -ETIMEDOUT
  ("master 0 queue 0 timeout").
- Debug (imx374 debug_hold_power=1): S2MPB02 LDO5/9/15 really on (0xa4/0xb0/0xd8), MCLK4 19.2 MHz enabled, titan GDSC on
  (CCI power/control=on), reset gpio24 high, gpio114/115 func cci_i2c pull-up: BOTH LINES READ LOW, every transfer times out.
  Front AF rail LDO16 on (0x2d=0xd8) did not help (restored 0x58). No camera GPIO LDO exists in the stock DT.
- Next options: boot stock (Magisk) to read real pin/bus state; or try the tele S5K3K1 on CCI0.

## 2026-10-20 attempt 40: drop clk_ignore_unused/pd_ignore_unused (REVERTED to 39: no screen)
- Cmdline is built in (CONFIG_CMDLINE + CMDLINE_FORCE=y; DT bootargs ignored): kernel #22 with
  "fw_devlink.sync_state=timeout" instead of "clk_ignore_unused pd_ignore_unused". Boot img f8a5270a...
- Result: unused clocks/PDs disabled, gcc/gpucc/interconnects "Timed out. Forcing sync_state()", Wi-Fi/audio/vibration OK,
  BUT msm-mdss: "deferred probe timeout, ignoring dependency" -> probe failed -110 -> no display (KWin running).
  The sync_state timeout also cut off display probing that was still waiting for a supplier.
- Reverted to attempt 39 (kernel #21). Staging, .config (config-build21.pre-cmdline) and DT bootargs restored.
- Next try: keep the timeout but long enough for msm/panel modules (e.g. deferred_probe_timeout=60), or drop the two
  ignore flags without the timeout and see what still disables.
- Before-temps were skewed (camera debug left front rails + MCLK on): CPU 46 C idle vs 36.5 C baseline (attempt 32).
