# Setup before you start (Plasma Mobile build) - do this once, then nothing needs installing mid-process

Goal: get every dependency and the pmOS image configured **before** the first flash. UI: **Plasma Mobile**.
Tested host: Pop!_OS / Ubuntu 24.04, pmbootstrap 3.12.1, kernel 7.2.11. Phone: Galaxy S22 SM-S901E, bootloader already unlocked
(never flash Android 16 / One UI 8 firmware - it relocks the bootloader).

## 1. PC dependencies (one command, needs sudo)

```
bash tools/setup-host.sh
```
That installs: `adb android-sdk-platform-tools-common udev git rsync make build-essential bc flex bison libssl-dev libelf-dev
libncurses-dev openssl gcc-aarch64-linux-gnu device-tree-compiler zstd lz4 cpio python3 python3-venv python3-tk pipx libusb-1.0-0
network-manager`, installs `pmbootstrap` with pipx, adds you to `plugdev`, and stops ModemManager. Log out/in once afterwards.
Python libraries go in a venv or pipx (Ubuntu refuses system-wide `pip install`: "externally-managed-environment").
For the Odin tool we used Odin4-GUI (github.com/Ragekill3377/Odin4-GUI) in its own venv (needs `python3-tk`; we ran it with sudo for USB access).

## 2. EFS backups, then install TWRP IMMEDIATELY (do not skip, do not reorder)

1. **Back up the phone-specific partitions first**: `efs`, `sec_efs`, `persist`, `modemst1`, `modemst2`, `fsg`, `fsc`
   (IMEI, Wi-Fi/Bluetooth addresses, sensor calibration live here and cannot be recreated). We read them as raw images with `dd` over
   `adb shell` from a Magisk-rooted Android (a Magisk-patched AP flashed with Odin); a TWRP shell can do the same
   (`dd if=/dev/block/by-name/efs of=/sdcard/efs.img`). Pull them to the PC, keep **SHA256SUMS**, store copies off the PC. Never write to these partitions again.
2. **Install TWRP as soon as the backups are safe**: flash the r0q TWRP to the **recovery** slot with Odin
   (AP slot, untick Auto Reboot, then boot straight into recovery with the button combo). Leave it there for good.
   This is the key step: from here on you reboot between Linux and TWRP with software only (`reboot2 recovery` / `adb reboot`),
   flash only `boot` with `adb push` + `dd`, and read crash logs from TWRP - no more button combos or Odin for every attempt.
   Odin is then only needed to restore stock firmware. Details: [flashing-workflow.md](flashing-workflow.md).
3. Only now start the kernel/pmOS work below. (We learned this the hard way: a boot that hangs with no logs and no recovery path costs hours.)

## 3. Sources (no spaces in any path!)

```
mkdir -p ~/s22-kernel && cd ~/s22-kernel
# Linux 7.2.11 from kernel.org, then apply kernel/upstream-patches/* and kernel/0001-r0q-debug-ramoops-and-console.patch
# copy kernel/panel-samsung-s6e3fac.c to drivers/gpu/drm/panel/ and add its Kconfig/Makefile lines (see docs/boot-log.md "Attempt 13")
# copy kernel/sm8450-samsung-r0q.dts to arch/arm64/boot/dts/qcom/
# use kernel/config-r0q.aarch64 (or r0q.fragment on top of defconfig); always: scripts/config ... && make olddefconfig </dev/null
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image.gz modules qcom/sm8450-samsung-r0q.dtb
```
uniLoader (secondary loader): clone it OUTSIDE this repo and let `tools/build-uniloader.sh` build in a copy
(its AGENTS.md forbids LLM-generated changes in its tree).

## 4. pmbootstrap with Plasma Mobile

```
pmbootstrap init
```
Answers that matter:
- **Device:** `samsung-r0q` (lives in pmaports `device/testing`; accept the "testing" warning)
- **User interface:** **`plasma-mobile`**   (we first ran with `none` and added it later; picking it at init avoids a 1.3 GB install on the phone)
- Init system: `openrc`; username of your choice (pmOS UID 10000); timezone/locale yours.

Then edit the device package so it matches what we flashed
(`~/.local/var/pmbootstrap/cache_git/pmaports/device/testing/device-samsung-r0q/deviceinfo`), add these two lines:

```
deviceinfo_rootfs_image_sector_size="4096"          # UFS on this phone has 4K sectors, otherwise pmOS cannot find its rootfs
deviceinfo_usb_network_host_addr="02:22:52:00:00:02" # fixed PC-side MAC -> stable interface name enx022252000002
```
(the rest of the stock deviceinfo is already `header_version="4"`, `flash_pagesize="4096"`, heimdall partitions BOOT/USERDATA.)

Install, preloading the packages we ended up needing so you don't install them over SSH later:

```
pmbootstrap install --add bluez,bluez-btmgmt,linux-firmware-ath11k,linux-firmware-qca,i2c-tools,mesa-demos,chrony
```
The image is written to `~/.local/var/pmbootstrap/chroot_native/home/pmos/rootfs/samsung-r0q.img` (flash only into `userdata`).

## 5. PC network profile for the USB link (NetworkManager shared mode)

```
nmcli con add type ethernet ifname enx022252000002 con-name "S22 USB (pmOS)" \
  ipv4.method shared ipv4.addresses 172.16.42.2/24 ipv4.never-default yes ipv6.method ignore
```
`never-default` matters: without it the phone profile can steal your PC's default route. Never `nmcli device connect` an existing wired profile onto it.

## 6. Phone-side files to put on the rootfs (before the first boot, or over SSH)

From `phone-config/`: module blocklist (`qcrypto` MUST stay blocked - it hangs the phone), `msm separate_gpu_kms=1`, USB internet/NTP script,
the clock save/restore service (the RTC is read-only), and the tinydm autologin line. Enable with
`rc-update add s22-clock boot` and make sure `tinydm` (not greetd) is in the default runlevel for Plasma-only login:
`tinydm-set-session -f -s /usr/share/wayland-sessions/plasma-mobile.desktop`.
Firmware (not in this repo): extract the GPU zap shader / `a730_sqe.fw` / `gmu_gen70000.bin` and the Wi-Fi/BT blobs from your own phone
(paths in docs/boot-log.md "Attempt 12"); put them under `/lib/firmware/...` as listed there. Bluetooth address comes from your own EFS (`/efs/bluetooth/bt_addr`):
`btmgmt public-addr <addr>` at boot (see `phone-config/etc/local.d`, boot-log "Bluetooth address").

## 7. Flash

Backups and TWRP are already done (section 2). Follow
[flashing-workflow.md](flashing-workflow.md). After first boot: replug the USB cable, `ssh <user>@172.16.42.1`.
