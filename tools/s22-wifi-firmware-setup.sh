#!/bin/sh
# Run ON THE PHONE as root. Makes ath11k use Samsung's own Wi-Fi firmware + board file + regdb,
# which (with the ath11k cold boot calibration patch) is what makes Wi-Fi transmit on the S22.
# Source files: the phone's own stock firmware (vendor/firmware_mnt), staged in /lib/firmware/s22-stock/qca6490
# (amss20.bin -> amss.bin, m3.bin, regdb.bin, bdwlang.elf).
set -e
Q=${1:-/lib/firmware/s22-stock/qca6490}
U=/lib/firmware/updates/ath11k/WCN6855/hw2.1
mkdir -p "$U"
cp "$Q/amss20.bin" "$U/amss.bin"
cp "$Q/m3.bin" "$Q/regdb.bin" "$U/"
# Board file: soc_id has the "GF" bit (0x11) and board_id is 0xff -> Samsung loads bdwlang.elf
cp "$Q/bdwlang.elf" "$U/board.bin"
# The generic upstream board-2.bin would otherwise win; an EMPTY override is ignored by the
# firmware loader, so use a small invalid one: ath11k fails to parse it and falls back to board.bin.
printf 'NOT-A-BOARD-2-FILE' > "$U/board-2.bin"
echo "done: $(ls "$U" | tr '\n' ' ')"
echo "reload: modprobe -r ath11k_pci ath11k && modprobe ath11k_pci"
