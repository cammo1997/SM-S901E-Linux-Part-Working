#!/bin/bash
# Build uniLoader for the Galaxy S22 (r0q), following uniLoader's README.
# Run this yourself:  bash build-uniloader.sh
# It builds in a copy of uniLoader at ~/s22-kernel/uniloader and edits no uniLoader code.

set -e
W="$(cd "$(dirname "$0")" && pwd)"
ORIG="$W/uniLoader/upstream"
SRC="$HOME/s22-kernel/uniloader"   # space-free copy: make cannot build in paths with spaces
PMOS="$W/out/pmos"
OUT="$W/out/uniloader"

for f in vmlinuz initramfs sm8450-samsung-r0q.dtb; do
    [ -f "$PMOS/$f" ] || { echo "Missing $PMOS/$f"; exit 1; }
done
command -v aarch64-linux-gnu-gcc >/dev/null || { echo "Install gcc-aarch64-linux-gnu first"; exit 1; }

mkdir -p "$OUT"
rm -rf "$SRC"
cp -r "$ORIG" "$SRC"

echo "[1/4] Preparing blobs (README: Image, dtb, ramdisk)"
zcat "$PMOS/vmlinuz" > "$OUT/Image"
cp "$OUT/Image"                    "$SRC/blob/Image"
cp "$PMOS/sm8450-samsung-r0q.dtb"  "$SRC/blob/dtb"
cp "$PMOS/initramfs"               "$SRC/blob/ramdisk"

cd "$SRC"
touch "$OUT/.stamp"

echo "[2/4] make r0q_defconfig"
make ARCH=aarch64 CROSS_COMPILE=aarch64-linux-gnu- r0q_defconfig

echo "[3/4] make"
make ARCH=aarch64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)"

echo "[4/4] New files produced by the build:"
find . -newer "$OUT/.stamp" -type f \( -name '*.bin' -o -name '*.elf' -o -name '*.img' -o -name 'uniLoader*' \) -not -path './.git/*' -exec ls -l {} \;
for f in $(find . -newer "$OUT/.stamp" -type f \( -name '*.bin' -o -name '*.img' -o -name 'uniLoader*' \) -not -path './.git/*' -not -name '*.o' -not -name '*.d' -not -name '*.cmd'); do
    cp "$f" "$OUT/"
done
echo
echo "Copied build outputs to: $OUT"
ls -l "$OUT"
echo "Tell Claude the file names above, and it will help you package them into a boot.img."
