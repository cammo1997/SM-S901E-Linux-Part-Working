#!/bin/sh
# SPDX-License-Identifier: MIT
# Extract SM-S901E (r0q) cellular modem + IPA v5.1 firmware and populate
# read-only EFS file copies for rmtfs (/var/lib/rmtfs).
#
# SAFETY RULE: Never write to raw efs, sec_efs, persist, modemst1/2, fsg, or fsc!
# All mounts and dd reads below are strictly read-only.

set -eu

FW_DST="${FW_DST:-/lib/firmware/qcom/sm8450/samsung/r0q}"
RMTFS_DIR="${RMTFS_DIR:-/var/lib/rmtfs}"
EFS_BACKUP_DIR="${1:-}"

mkdir -p "$FW_DST" "$RMTFS_DIR"

# 1. Copy apnhlos firmware first (provides BL/TZ-matched ipa_fws.mdt + DSP images)
APNHLOS_DEV=""
for p in /dev/disk/by-partlabel/apnhlos /dev/sda19; do
	if [ -b "$p" ]; then
		APNHLOS_DEV="$p"
		break
	fi
done

if [ -n "$APNHLOS_DEV" ]; then
	mkdir -p /mnt/s22-apnhlos
	if ! mountpoint -q /mnt/s22-apnhlos; then
		modprobe vfat 2>/dev/null || true
		mount -t vfat -o ro "$APNHLOS_DEV" /mnt/s22-apnhlos
	fi
	cp -af /mnt/s22-apnhlos/image/ipa_fws.* "$FW_DST/" 2>/dev/null || true
	umount /mnt/s22-apnhlos || true
fi

# 2. Copy modem partition firmware (modem.mdt, modem.b*, modemr.jsn, modem_pr/)
#    Keep apnhlos's ipa_fws.* intact so PAS ID 15 matches the bootloader TZ chain.
MODEM_DEV=""
for p in /dev/disk/by-partlabel/modem /dev/sda24; do
	if [ -b "$p" ]; then
		MODEM_DEV="$p"
		break
	fi
done

if [ -n "$MODEM_DEV" ]; then
	mkdir -p /mnt/s22-modem
	if ! mountpoint -q /mnt/s22-modem; then
		modprobe vfat 2>/dev/null || true
		mount -t vfat -o ro "$MODEM_DEV" /mnt/s22-modem
	fi
	for item in /mnt/s22-modem/image/*; do
		base="$(basename "$item")"
		case "$base" in
			ipa_fws.*)
				# Only copy if apnhlos did not already provide ipa_fws
				[ -e "$FW_DST/$base" ] || cp -af "$item" "$FW_DST/"
				;;
			*)
				cp -af "$item" "$FW_DST/"
				;;
		esac
	done
	umount /mnt/s22-modem || true
fi

# 3. Populate read-only EFS copies in /var/lib/rmtfs (NEVER pass raw partitions to rmtfs)
copy_ro_efs() {
	src="$1"
	dst="$2"
	rm -f "$dst"
	dd if="$src" of="$dst" bs=4096 status=none
	chown root:root "$dst"
	chmod 0444 "$dst"
}

if [ -n "$EFS_BACKUP_DIR" ] && [ -f "$EFS_BACKUP_DIR/modemst1.img" ]; then
	echo "Installing read-only rmtfs files from backup directory: $EFS_BACKUP_DIR"
	copy_ro_efs "$EFS_BACKUP_DIR/modemst1.img" "$RMTFS_DIR/modem_fs1"
	copy_ro_efs "$EFS_BACKUP_DIR/modemst2.img" "$RMTFS_DIR/modem_fs2"
	copy_ro_efs "$EFS_BACKUP_DIR/fsg.img"      "$RMTFS_DIR/modem_fsg"
	copy_ro_efs "$EFS_BACKUP_DIR/fsc.img"      "$RMTFS_DIR/modem_fsc"
elif [ ! -s "$RMTFS_DIR/modem_fs1" ] && [ -b /dev/disk/by-partlabel/modemst1 ]; then
	echo "Snapshotting read-only rmtfs file copies from partition labels..."
	copy_ro_efs /dev/disk/by-partlabel/modemst1 "$RMTFS_DIR/modem_fs1"
	copy_ro_efs /dev/disk/by-partlabel/modemst2 "$RMTFS_DIR/modem_fs2"
	copy_ro_efs /dev/disk/by-partlabel/fsg      "$RMTFS_DIR/modem_fsg"
	copy_ro_efs /dev/disk/by-partlabel/fsc      "$RMTFS_DIR/modem_fsc"
fi

# Create zeroed modem_fsc fallback if fsc is tiny/absent
if [ ! -e "$RMTFS_DIR/modem_fsc" ]; then
	dd if=/dev/zero of="$RMTFS_DIR/modem_fsc" bs=1024 count=4 status=none
	chmod 0444 "$RMTFS_DIR/modem_fsc"
fi

sync
echo "Modem firmware in $FW_DST:"
ls -lh "$FW_DST/modem.mdt" "$FW_DST/ipa_fws.mdt" 2>/dev/null || true
echo "Read-only rmtfs EFS copies in $RMTFS_DIR:"
ls -lh "$RMTFS_DIR"/modem_fs*
