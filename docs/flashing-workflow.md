# Flash / reboot / recovery workflow (the headache saver)

Phone: Samsung Galaxy S22 SM-S901E, bootloader unlocked. No fastboot, and no need to fight the button combos once this is set up.
Idea: **TWRP lives in the recovery slot permanently**, Linux boots from `boot`. You flip between them with software only.

## One-time setup
- Flash TWRP for r0q (fingerprint `samsung/twrp_r0q/r0q`) to the **recovery** slot with Odin and leave it there.
- On the PC: `adb` installed. Put your SSH key on the phone (pmOS user, passwordless sudo via /etc/sudoers.d).
- Build the reboot helper (busybox `reboot recovery` ignores its argument!):
  `gcc -static -o reboot2 tools/reboot2.c` (arm64: use `aarch64-linux-gnu-gcc`), copy to the phone as `~/reboot2`.
  It calls `reboot(LINUX_REBOOT_CMD_RESTART2, arg)`, so `~/reboot2 recovery` really reboots into recovery.
- Before ANY flash on the PC: `sudo systemctl stop ModemManager` (it grabs the USB device). Never unplug mid-flash.

## Linux -> TWRP (no buttons)
```
ssh cammo@172.16.42.1 'sync; sudo ~/reboot2 recovery'      # wrap in `timeout 15 ssh ...`: ssh hangs on reboot
adb wait-for-recovery        # or: until [ "$(adb get-state)" = recovery ]; do sleep 3; done
adb devices                  # shows the serial + "recovery"
```
Do NOT run `~/reboot2` with no argument: it reuses the last reboot reason and sent us back to TWRP once.

## Build and flash a new boot image (only `boot` is ever written)
```
bash build-uniloader.sh                              # builds uniLoader (Image+DTB+ramdisk embedded)
python3 tools/pack-boot-img.py out/uniloader/uniLoader boot.img
sha256sum boot.img                                   # note it
adb push boot.img /tmp/boot.img
adb shell 'sha256sum /tmp/boot.img'                  # must match the PC
adb shell 'readlink -f /dev/block/by-name/boot'      # sda25 on this phone - check before writing!
adb shell 'dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=4096 && sync'
adb shell 'dd if=/dev/block/by-name/boot bs=4096 count=24576 2>/dev/null | sha256sum'   # read-back must match too
adb reboot                                           # back into Linux
```
`boot.img` is exactly 100663296 bytes = 24576 x 4096, hence `count=24576` for the read-back.

## After the phone boots Linux
- **Replug the USB cable** (the phone has no VBUS detection, so the USB gadget only comes up after a replug).
- PC side: a NetworkManager profile in *shared* mode on the phone's interface (we fixed the phone's MAC `02:22:52:00:00:02` so the interface
  name `enx022252000002` never changes) gives the phone 172.16.42.1 and internet. Keep it `never-default` so it cannot steal your own network.
- Wait for SSH: loop `ssh -o ConnectTimeout=4 cammo@172.16.42.1 cat /proc/uptime` until uptime < 400.

## TWRP -> Linux, other reboots
- `adb reboot` from TWRP = normal boot (Linux). `adb reboot recovery` = stay in TWRP. TWRP menu: Reboot -> Download for Odin
  (`adb reboot download` also exists on Samsung TWRP builds - not needed here).
- If Linux is hung: hold **Power + Volume Down ~10 s** to force a restart.
- Odin (Download mode) is only needed to restore Android/stock recovery or to re-flash TWRP.

## Reading logs after a crash (TWRP)
Mount the phone's storage in TWRP and `adb pull`/`adb shell cat` the logs; with ramoops configured, a panic log survives in
`/sys/fs/pstore` on the next boot. TWRP **cannot mount the pmOS ext4 rootfs** (orphan_file feature needs a newer kernel), so
do not rely on reading rootfs files from recovery - use SSH on the booted system instead.

## Hard rules
- Only flash: `boot` (and, for a full reinstall, vendor_boot / dtbo / vbmeta + rootfs into userdata).
- Never write: bootloader/param partitions, `efs`, `sec_efs`, `persist`, `modemst1`, `modemst2`, `fsg`, `fsc`.
- Never flash Android 16 / One UI 8 firmware or any newer bootloader: it permanently relocks the bootloader.
- Back up `efs sec_efs persist modemst1 modemst2 fsg fsc` before experimenting (we did, via TWRP).
- Never hot-reload display drivers (unbinding `msm-mdss` crashed the phone): install the module and reboot.
- Module-only tests need no flash: `scp` the `.ko` over, `insmod`/`modprobe`, read `dmesg`. Only DT/kernel-image changes need the loop above.
