# Credits

This port stands on other people's work. Thank you to:

| Project / people | Link | License | What we used |
|---|---|---|---|
| Linux kernel developers | https://kernel.org | GPL-2.0 | Kernel 7.2.11, SM8450 device tree, ath11k, AudioReach/q6apm, CS35L41, dwc3, panel drivers |
| Samsung Open Source Release Center | https://opensource.samsung.com | GPL-2.0 | SM-S901E kernel source and stock device tree values (amp wiring, panel 120 Hz, MAX77705 IRQ pin, Wi-Fi cold boot cal, memory map) |
| aaronsb – Galaxy Tab S8+ (SM-X800) mainline port | https://github.com/aaronsb/sm-x800-linux | GPL-2.0 / MIT | MAX77705 Type-C + charger patches, dwc3 patches, MI2S fmt/sysclk patch, Wi-Fi rail values, audio topology (`kernel/audio-wip/tplg.bin`) |
| ivoszbg – uniLoader | https://github.com/ivoszbg/uniLoader | GPL-2.0 | Secondary bootloader used to boot mainline from Samsung's bootloader |
| postmarketOS | https://postmarketos.org · https://gitlab.postmarketos.org/postmarketOS/pmbootstrap | GPL-3.0 / MIT | Distribution, pmbootstrap, initramfs USB networking |
| KDE Plasma Mobile | https://plasma-mobile.org | GPL / LGPL | Phone user interface, Plasma keyboard, KRecorder, Discover |
| Samsung Galaxy S9 (starqltechn) mainline port | `arch/arm64/boot/dts/qcom/sdm845-samsung-starqltechn.dts` in mainline | GPL-2.0 | MAX77705 device tree template and mainline MAX77705 MFD/charger drivers |
| Viveris – uMTP-Responder | https://github.com/viveris/uMTP-Responder | GPL-3.0 | MTP file transfer (`umtprd`) |
| Nura wiki – Samsung Galaxy S22 5G (samsung-r0q) | https://wiki.nura.eco/wiki/Samsung_Galaxy_S22_5G_(samsung-r0q) | – | Reference notes for r0q |
| TWRP | https://twrp.me | GPL-3.0 | Recovery used for flashing and EFS backup |
| Rob Clark & Freedreno / Linux `drm/msm` contributors | `drivers/gpu/drm/msm/` in mainline · https://gitlab.freedesktop.org/drm/msm | GPL-2.0-only | Qualcomm Adreno 6xx/7xx KMS/GPU driver, GMU/HFI power management, syncobj, `VM_BIND`, and devfreq infrastructure optimized in `local-msm-adreno730-w10-gpu-opt.patch` |
| Mesa 3D, Turnip Vulkan & `ir3` compiler contributors | https://mesa3d.org · https://gitlab.freedesktop.org/mesa/mesa | MIT | Turnip `FD730` Vulkan 1.4 ICD, GMEM/SYSMEM renderpass autotuner, `ir3` shader compiler, NIR algebraic passes, and Gallium Freedreno/Zink drivers (`tools/mesa-26.3.0-turnip-ir3-adreno730-w5.patch`) |
| Linaro / Qualcomm upstream contributors | https://git.codelinaro.org/linaro/qcomlt | GPL-2.0 | SM8450 mainline support, AudioReach, remoteproc, QRTR/rmtfs |
| Flathub | https://flathub.org | – | App store (Google Chrome ARM64) |

Samsung firmware is proprietary and belongs to Samsung / Qualcomm; it is not redistributed here.
If your work is used here and is not credited correctly, please open an issue.
