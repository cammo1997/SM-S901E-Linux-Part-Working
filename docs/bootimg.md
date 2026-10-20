# Step 2: stock boot image analysis (S901EXXSCFYH1)

Parsed directly with Python (not pmbootstrap bootimg_analyze, which needs `pmbootstrap init` first). Read-only.

## boot.img (partition `boot`, 96 MiB)
- magic ANDROID!, **header_version 4**, header_size 1584
- kernel_size 41,490,944 (about 39.6 MiB), ramdisk_size 1,978,966 (about 1.9 MiB)
- os_version Android 12.0.0, patch level 2025-08 (GKI base)
- cmdline: empty (header v4 has no cmdline in boot; it lives in vendor_boot)
- Kernel: arm64 `Image` (ARM\x64 magic present), image_size 0x2a50000, text_offset 0

## vendor_boot.img (partition `vendor_boot`, 96 MiB)
- magic VNDRBOOT, **header_version 4**, page_size 4096, header_size 2128
- kernel_addr 0x8000, ramdisk_addr 0x2000000, tags_addr 0x1e00000, dtb_addr 0x1f00000
  (these are offsets; with the usual base 0x80000000 that gives kernel 0x80008000, ramdisk 0x82000000, dtb 0x81f00000)
- vendor_ramdisk_size 21,467,595 (about 20.5 MiB), dtb_size 1,721,428 (about 1.6 MiB), 1 ramdisk table entry, bootconfig 164 bytes
- name: SRPUI14A012
- cmdline: `video=vfb:640x400,bpp=32,memsize=3072000 printk.devkmsg=on firmware_class.path=/vendor/firmware_mnt/image console=null bootconfig loop.max_part=7`

## Notes / open questions
- Header v4: the bootloader loads kernel from `boot` and ramdisk + DTB from `vendor_boot`. Our boot.img therefore needs to be v4 with uniLoader as the "kernel".
- Decision still open: keep stock vendor_boot (it carries the stock DTB and the vendor ramdisk), flash a minimal one, or put everything in boot.img.
- RESOLVED: uniLoader `PAYLOAD_ENTRY=0x80b900000` is NOT a typo. Stock DT shows RAM above 4 GiB starts at 0x8_0000_0000
  (memory node banks: 0x80000000 +0x6a000000, 0xf1c00000 +0xe400000, 0x800000000 +0x3ab00000, 0x840000000 +0x140000000, 0x83b100000 +0x1e00000),
  so 0x8_0b90_0000 is a valid 36-bit address in the upper bank.

## Stock device tree (recon/live.dts, decompiled with dtc, 38,764 lines)
- model "Samsung R0Q PROJECT (board-id,12)", compatible `qcom,waipio-mtp`, `qcom,waipio`, `qcom,mtp` (SM8450 = waipio)
- splash_region (cont_splash) at **0xb8000000, size 0x2b00000 (43 MB)**: matches uniLoader's simplefb address, so early screen output should work
- Other fixed regions: hyp 0x80000000, smem 0x80900000, cpucp 0x80b00000, adsp 0x84500000 (0x3b00000), mpss (modem) 0x8bc00000 (0x13200000),
  oem_vm 0xbb000000 (0x5000000), tags 0xe8900000, qtee 0xe9b00000. uniLoader TEXT_BASE 0x87000000 falls inside the adsp region (stock DT),
  which is fine while uniLoader runs, but the kernel DT must reserve/avoid it once Linux owns the memory.
- ramoops_region exists in stock DT (disabled), 2 MB, useful later for pstore crash logs.
- Samsung sec_debug carve-outs at 0x800100000 and 0x8001FF000 (upper bank).
- Still to extract for the DTS: UFS, USB (dwc3), display panel (DSI), PMIC/regulators.
