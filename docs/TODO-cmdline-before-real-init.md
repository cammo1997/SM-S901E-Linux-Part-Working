# TODO before switching back to the real pmOS /init

Attempt 3 (canary) uses `CONFIG_CMDLINE_FORCE=y`, which makes the kernel ignore every other command line
(boot image, vendor_boot, DTB `bootargs`). Fine for the canary, but the real pmOS init finds its rootfs
from command-line arguments. If they are missing it will not find the rootfs.

When the canary is replaced with the real init (`/init.pmos` -> `/init`, or back to the stock pmOS initramfs):

1. Either drop `CONFIG_CMDLINE_FORCE` (go back to the bootloader/DTB command line), or make the forced
   `CONFIG_CMDLINE` contain everything pmOS normally gets:
   - `deviceinfo_kernel_cmdline` from device-samsung-r0q/deviceinfo (currently empty)
   - the root-finding arguments pmbootstrap generated. From the 19:53 install (4K-sector image) (out/rootfs/vendor_boot.pmbootstrap.img, verified against the image filesystems):

     ```
     quiet pmos_boot_uuid=BOOT-PARTITION-UUID pmos_root_uuid=ROOT-PARTITION-UUID pmos_rootfsopts=defaults
     ```

     These UUIDs change every time `pmbootstrap install` runs. Re-read them from the newly
     exported vendor_boot.img (cmdline field, offset 28, 2048 bytes) after each install.
2. Keep the bring-up options while debugging:
   `clk_ignore_unused pd_ignore_unused ignore_loglevel console=tty0 panic=0` (drop `quiet`, and
   drop `initcall_debug` once boot is understood because it is very verbose).
3. The rootfs (samsung-r0q.img) is not flashed to userdata yet, and the r0q DTS has no UFS node,
   so the real init cannot find the rootfs until UFS is added and the rootfs is flashed anyway.
