# Step 1 research: existing r0q work (2026-10-17)

## uniLoader
- `github.com/codechara/uniLoader` does NOT exist ("Repository not found"). The plan's fork reference was wrong.
- Upstream `github.com/ivoszbg/uniLoader` (cloned to `uniLoader/upstream/`) already supports r0q:
  - `configs/r0q_defconfig`
  - `board/samsung/board-r0q.c`
  - Kconfig entry `SAMSUNG_R0Q` ("Samsung Galaxy S22"), depends on `SM8450`
- Commits touching r0q:
  - 6557f34 Eric Goncalves, 2025-10-02, "board: Add support for the Samsung Galaxy S22"
  - 467d16f Violet, 2025-10-29, "r0q: Enable Linux kernel header"
  - 31f118c Ivaylo Ivanov, 2026-05-27, driver framework refactor

### r0q_defconfig
```
CONFIG_POSITION_INDEPENDENT=y
CONFIG_LINUX_KRNL_HEADER_IMG=y
CONFIG_SM8450=y
CONFIG_SAMSUNG_R0Q=y
CONFIG_TEXT_BASE=0x87000000
CONFIG_PAYLOAD_ENTRY=0x80b900000      # NOTE: 9 hex digits, check this against the stock DT / kernel load address
CONFIG_RAMDISK_ENTRY=0xb6915000
```

### board-r0q.c
- simplefb: ARGB8888, 1080x2340, stride 4, address 0xb8000000
- no other board ops (no SoC setup beyond the SM8450 soc code)
- board name "samsung-r0q"

## Mainline device tree
**UPDATE (Step 4): a mainline r0q DTS DOES exist.** `arch/arm64/boot/dts/qcom/sm8450-samsung-r0q.dts` is in Linux v7.2.11
(model "Samsung Galaxy S22 5G", compatible "samsung,r0q" / "qcom,sm8450"). It is minimal: simple-framebuffer at 0xb8000000
(1080x2340), vph_pwr, PM8350/PM8350C regulators, USB peripheral mode (USB 2.0 only). No UFS, panel, touch, battery, Wi-Fi, GPU yet.
Kernel source: ~/s22-kernel/linux (v7.2.11, symlinked at ./kernel/linux). Kept outside the workspace because the kernel
build refuses source paths containing spaces. Toolchain: ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-.
The older note below (not found) came from an unauthenticated GitHub search and was wrong.
- No `sm8450-samsung-r0q.dts` found. GitHub repository search (unauthenticated) returned zero results for
  `sm8450-samsung-r0q`, `samsung-r0q postmarketos`, `r0q sm8450 linux`. Code search needs auth, so this is NOT proof none exists.
- Leads: authors of the uniLoader r0q commits (Eric Goncalves, Violet) likely have a kernel tree/DTS somewhere.
- Related reference: Galaxy Tab S8+ (SM-X800, SM8450) mainline port at github.com/aaronsb/sm-x800-linux.

## pmaports
- No `device-samsung-r0q` anywhere in pmaports (device/testing, community, main).
- No SM8450 kernel package: `device/testing` has linux-postmarketos-qcom-sm7125/sm8150/sm8250 but no sm8450.
- So Step 3 (create the device package) and the kernel package are ours to write.

## Next
- Write our own DTS from mainline `sm8450.dtsi` + `recon/live.dtb`, using the uniLoader framebuffer address.
- Ask the uniLoader r0q authors (GitHub issues/Matrix #mobile-linux) whether they have a kernel/DTS.
