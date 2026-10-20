#!/usr/bin/env python3
"""Wrap a uniLoader binary into an Android boot image v4 (the format Samsung's ABL accepts here).

usage: pack-boot-img.py out/uniloader/uniLoader boot.img

Header: 'ANDROID!', kernel_size=len(uniLoader), ramdisk_size=0, os_version=0x18000198, header_size=1584, header_version=4.
The kernel (uniLoader with Image + DTB + ramdisk embedded) is padded to 4096 and the file is zero-padded to
100663296 bytes (24576 x 4 KiB = the size of the boot partition, so dd + read-back checksums line up).
(Ubuntu's mkbootimg was broken for us - missing 'gki' module - hence this script.)
"""
import struct
import sys

PAGE = 4096
BOOT_SIZE = 100663296

kernel = open(sys.argv[1], 'rb').read()
header = bytearray(PAGE)
struct.pack_into('<8s10I', header, 0, b'ANDROID!', len(kernel), 0, 0x18000198, 1584, 0, 0, 0, 0, 4, 0)
image = bytes(header) + kernel
image += b'\0' * (-len(image) % PAGE)
assert len(image) <= BOOT_SIZE, "uniLoader too big for the boot partition"
image += b'\0' * (BOOT_SIZE - len(image))
open(sys.argv[2], 'wb').write(image)
print(f"wrote {sys.argv[2]}: kernel {len(kernel)} bytes, total {len(image)}")
