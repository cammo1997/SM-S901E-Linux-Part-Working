from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table,
                                TableStyle, PageBreak, Preformatted)

OUT = "/home/cammo/Documents/Projects/S22 SM-S901E Linux/S22-Mainline-Linux-Guide.pdf"

ss = getSampleStyleSheet()
H1 = ParagraphStyle("H1", parent=ss["Heading1"], spaceBefore=10, spaceAfter=6, textColor=colors.HexColor("#1f3b5c"))
H2 = ParagraphStyle("H2", parent=ss["Heading2"], spaceBefore=8, spaceAfter=4, textColor=colors.HexColor("#2b5a87"))
B = ParagraphStyle("B", parent=ss["BodyText"], fontSize=9.5, leading=13)
SM = ParagraphStyle("SM", parent=B, fontSize=8.5, leading=11)
BOX = ParagraphStyle("BOX", parent=B, backColor=colors.HexColor("#fff4e5"), borderColor=colors.HexColor("#e0a040"),
                     borderWidth=0.8, borderPadding=6, spaceBefore=6, spaceAfter=10)
CODE = ParagraphStyle("CODE", fontName="Courier", fontSize=8, leading=10, backColor=colors.HexColor("#f2f2f2"),
                      borderPadding=4, spaceBefore=4, spaceAfter=8)

def P(t, s=B): return Paragraph(t, s)
def bullets(items, s=B): return [Paragraph("&bull; " + i, s) for i in items]
def code(t): return Preformatted(t, CODE)

def table(rows, widths, head=True):
    data = [[Paragraph(str(c), SM) for c in r] for r in rows]
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    st = [("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#999999")),
          ("VALIGN", (0, 0), (-1, -1), "TOP"),
          ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4)]
    if head:
        st.append(("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#dde6f0")))
    t.setStyle(TableStyle(st))
    return t

story = []
story += [P("Mainline Linux on a Samsung Galaxy phone", ss["Title"]),
          P("Field guide from the Galaxy S22 (SM-S901E, Snapdragon 8 Gen 1) postmarketOS port - October 2026", B),
          Spacer(1, 6)]

story.append(P("<b>Read this first if you have a Galaxy S10+.</b> The S10+ is a very different phone underneath. "
               "Australian/European models are <b>SM-G975F (Samsung Exynos 9820)</b>; US models are <b>SM-G975U (Snapdragon 855)</b>, "
               "and US Snapdragon Samsung bootloaders generally cannot be unlocked. Check the model number in "
               "Settings &gt; About phone before anything else. Almost every driver fix in this guide is specific to the "
               "S22's Qualcomm SM8450 chip and will NOT carry over. What carries over is the <b>method</b>: the safety rules, "
               "the flash loop, how to read the stock firmware, and how to find a reference port. Part F is the "
               "starting checklist for an S10+.", BOX))


story.append(P("Part A - Start to finish: unlock, root, EFS backup, TWRP", H1))
story.append(P("Do these steps <b>in this order</b> and do not start any Linux work until step A5 is done. "
               "A boot that hangs with no logs and no recovery path costs hours; with TWRP in place every mistake is one reboot away from fixed.", B))

story.append(P("A0. Install everything first (one time, needs sudo once)", H2))
story.append(P("Install all of this <b>before</b> touching the phone, so nothing has to be installed mid-flash. "
               "Tested on Ubuntu 24.04 / Pop!_OS. Log out and back in afterwards (plugdev group).", B))
story.append(code("sudo apt-get update\n"
                  "sudo apt-get install -y adb android-sdk-platform-tools-common udev git rsync make \\\n"
                  "  build-essential bc flex bison libssl-dev libelf-dev libncurses-dev openssl \\\n"
                  "  gcc-aarch64-linux-gnu device-tree-compiler zstd lz4 cpio python3 python3-venv \\\n"
                  "  python3-tk pipx libusb-1.0-0 network-manager e2fsprogs patch curl kmod pciutils usbmuxd\n"
                  "sudo usermod -aG plugdev \"$USER\"\n"
                  "pipx install pmbootstrap && pipx ensurepath          # no sudo\n"
                  "# Odin4-GUI (Samsung flashing on Linux) in its own venv, no sudo to install:\n"
                  "git clone https://github.com/Ragekill3377/Odin4-GUI && cd Odin4-GUI/Odin4-GUI\n"
                  "sudo ./install_odin4.sh                               # installs the odin4 binary (one time)\n"
                  "python3 -m venv venv && venv/bin/pip install -r requirements.txt   # customtkinter, no sudo\n"
                  "sudo venv/bin/python main.py                           # run (needs USB access)\n"
                  "# before EVERY flash: ModemManager grabs the phone's USB port\n"
                  "sudo systemctl stop ModemManager"))
story += bullets([
    "Why each matters: <font face='Courier'>adb</font> (TWRP/flash loop), <font face='Courier'>lz4</font> (Samsung .lz4 images), "
    "<font face='Courier'>e2fsprogs</font> (debugfs: read MAC addresses from the EFS backup), "
    "<font face='Courier'>gcc-aarch64-linux-gnu, flex, bison, libssl-dev, libelf-dev, bc</font> (kernel build), "
    "<font face='Courier'>device-tree-compiler</font> (dtc), <font face='Courier'>zstd</font> (compressed modules), "
    "<font face='Courier'>python3-tk</font> (Odin4-GUI window), <font face='Courier'>pmbootstrap</font> (builds the postmarketOS image).",
    "Downloads to have ready: stock firmware for the <b>exact</b> model, region and build already on the phone "
    "(S22: SM-S901E XSA S901EXXSCFYH1; from samfw.com or SamMobile: BL_, AP_, CP_, CSC_, HOME_CSC_ .tar.md5), "
    "TWRP for the device codename (S22: r0q; S10+: beyond2lte), the Magisk APK (github.com/topjohnwu/Magisk), "
    "and Samsung's kernel source for the model (opensource.samsung.com).",
    "Keep kernel/source folders in a path <b>without spaces</b> (e.g. ~/s22-kernel) - the kernel build refuses spaces.",
])
story.append(P("Phone-side packages: add them when building the image so you never install over a slow link mid-debug:", B))
story.append(code("pmbootstrap init        # device samsung-r0q, UI plasma-mobile, init openrc\n"
                  "pmbootstrap install --add bluez,bluez-btmgmt,linux-firmware-ath11k,linux-firmware-qca,\\\n"
                  "  i2c-tools,mesa-demos,chrony,iw,wireless-regdb,rmtfs,tqftpserv,qrtr,qmi-utils,\\\n"
                  "  plasma-keyboard,nftables"))
story.append(P("(On the S22 deviceinfo we also set <font face='Courier'>deviceinfo_rootfs_image_sector_size=\"4096\"</font> - UFS uses 4K sectors - "
               "and a fixed USB MAC so the PC-side network interface name never changes.)", SM))

story.append(P("A1. Unlock the bootloader (wipes the phone)", H2))
story += bullets([
    "Settings &gt; About phone &gt; Software information: tap Build number 7x. Developer options &gt; <b>OEM unlocking</b> on.",
    "Power off. Hold <b>Volume Up + Volume Down</b> and plug in USB: the download-mode warning screen appears. "
    "<b>Long-press Volume Up</b> to unlock, confirm. The phone wipes and reboots.",
    "Set the phone up again (skip accounts), enable Developer options again and check OEM unlocking is on and greyed out. "
    "Knox trips permanently (Samsung Pay/Secure Folder stop working) - that is expected.",
])

story.append(P("A2. Root with Magisk (so you can read the EFS partitions)", H2))
story += bullets([
    "Copy the <b>AP_....tar.md5</b> to the phone, install the Magisk app, Magisk &gt; Install &gt; Select and patch a file &gt; the AP tar. "
    "Copy the resulting <font face='Courier'>magisk_patched-xxxxx.tar</font> back to the PC.",
    "Download mode again (Vol Up + Vol Down + USB). In Odin: <b>BL</b> = BL_, <b>AP</b> = magisk_patched tar, <b>CP</b> = CP_, "
    "<b>CSC</b> = <b>HOME_CSC</b>_ (HOME_ keeps data; plain CSC_ wipes). Start.",
    "Boot, open Magisk and let it finish setup. Check root: <font face='Courier'>adb shell su -c id</font> must show uid=0.",
])

story.append(P("A3. Back up EFS and friends - before anything else", H2))
story.append(P("These partitions hold the IMEI, Wi-Fi/Bluetooth MAC addresses and calibration. They are unique to the phone and "
               "cannot be recreated. Back them up as raw images:", B))
story.append(code("adb shell\n"
                  "su\n"
                  "cd /dev/block/by-name\n"
                  "for p in efs sec_efs persist modemst1 modemst2 fsg fsc; do\n"
                  "  dd if=$p of=/sdcard/$p.img\n"
                  "done\n"
                  "exit; exit\n"
                  "mkdir efs-backup && cd efs-backup\n"
                  "for p in efs sec_efs persist modemst1 modemst2 fsg fsc; do adb pull /sdcard/$p.img; done\n"
                  "sha256sum *.img > SHA256SUMS      # keep this file\n"
                  "adb shell 'rm /sdcard/*.img'"))
story += bullets([
    "Copy the folder somewhere off the PC too (USB stick / cloud). Check later with <font face='Courier'>sha256sum -c SHA256SUMS</font>.",
    "Partition names differ per device - list them first: <font face='Courier'>ls -l /dev/block/by-name</font>. "
    "Never write to these partitions again; on the S22 the modem now uses <i>file copies</i> of modemst1/2, fsg, fsc.",
    "Useful later: the Bluetooth address is in efs /bluetooth/bt_addr, the Wi-Fi MAC in efs /wifi/.mac.info "
    "(read from the image on the PC with <font face='Courier'>debugfs -R 'cat /wifi/.mac.info' efs.img</font>).",
])

story.append(P("A4. Install TWRP to the recovery partition - straight away", H2))
story += bullets([
    "Download mode. Odin: <b>AP</b> = the TWRP .tar, <b>untick Auto Reboot</b>, Start.",
    "When it says PASS, hold <b>Volume Down + Power</b> until the screen goes off, then immediately <b>Volume Up + Power</b> "
    "(with USB in) to boot straight into TWRP. If stock Android boots first it can replace TWRP with stock recovery.",
    "Follow the device's own TWRP page for extras (some Samsungs, including the S10 family, need vbmeta / a 'multidisabler' zip). "
    "TWRP stays in recovery for good: from now on you only ever flash <b>boot</b>.",
])

story.append(P("A5. The software-only flash loop", H2))
story += bullets([
    "Linux -&gt; TWRP: a tiny helper <font face='Courier'>reboot2.c</font> calling <font face='Courier'>reboot(LINUX_REBOOT_CMD_RESTART2, \"recovery\")</font> "
    "(busybox 'reboot recovery' ignores the argument). Always pass an argument.",
    "TWRP -&gt; Linux: <font face='Courier'>adb reboot</font>. If Linux hangs: hold Power + Volume Down ~10 s.",
    "Linux cannot be read from TWRP's old kernel (ext4 orphan_file) - read logs over SSH or from pstore instead.",
])
story.append(code("ssh phone 'sync; sudo ~/reboot2 recovery'\n"
                  "adb push boot.img /tmp/ && adb shell 'sha256sum /tmp/boot.img'          # must match the PC\n"
                  "adb shell 'readlink -f /dev/block/by-name/boot'                         # check before writing\n"
                  "adb shell 'dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=4096 && sync'\n"
                  "adb shell 'dd if=/dev/block/by-name/boot bs=4096 count=24576 | sha256sum'   # read-back must match\n"
                  "adb reboot"))

story.append(P("<b>A6. Do USB-C auto-detection as early as possible.</b> Without it the phone's USB network only comes up after a cable "
               "replug on every boot (no attach event reaches the USB controller). On Samsung phones with a MAX77705 companion chip the fix is the "
               "MAX77705 Type-C (CCIC) driver plus a USB role switch in the DT (dr_mode \"otg\", role-switch-default-mode \"peripheral\", "
               "connector endpoint linked to the USB controller). On the S22 we only did this at attempt 23 - doing it right after "
               "USB networking first works would have saved a replug on every single test boot. Patches: kernel-config/upstream-patches/tabs8/.", BOX))

story.append(P("Part B - Linux bring-up, in the order it happened", H1))
story.append(table([
    ["Step", "What we did", "Result"],
    ["1-3", "uniLoader in boot slot; DTB with explicit RAM ranges + Samsung no-map carve-outs; /dev/console in initramfs", "Kernel runs, initramfs shell"],
    ["4-5", "USB gadget networking; reboot2 helper", "Root shell over USB; button-free reboot to TWRP"],
    ["6-8", "UFS + clock controllers; pmOS rootfs rebuilt with 4K sectors", "postmarketOS boots from internal storage, SSH"],
    ["10-11", "Modules one at a time (qcrypto hangs the SoC - keep blocked); touch driver; antenna-switch rail", "Bluetooth, touch, battery"],
    ["12", "Adreno 730 with Samsung-signed zap shader from the phone", "GPU works"],
    ["13", "New S6E3FAC panel driver; Samsung source showed missing DCS 0x53", "Real display"],
    ["15-16", "Fuel gauge / charger DT links", "Battery %, charging"],
    ["17", "Found mainline bug: pcie-qcom parsed iommu-map with the wrong entry size", "Wi-Fi interrupts arrive, firmware loads"],
    ["19", "Matched the Galaxy Tab S8+ port's Wi-Fi DT (rails, no IOVA pool, mainline PHY, upstream firmware)", "Wi-Fi up and scanning"],
    ["20", "Modem: Samsung modem firmware + rmtfs serving EFS copies read-only", "Modem boots, QMI online"],
    ["21", "Removed mte_mem (512 MiB) that stock never reserves", "+512 MiB RAM"],
    ["-", "Panel module: stock fhd120hs timing + 3 command bytes", "120 Hz"],
    ["22", "nftables in kernel; hwclock service removed", "Firewall up, clean boot"],
    ["23", "MAX77705 Type-C driver (Tab S8+ patches), USB dual-role", "USB connects without replug"],
], [14*mm, 104*mm, 52*mm]))

story.append(P("Part C - What we found (problems and their causes)", H1))
story.append(table([
    ["Symptom", "Cause / fix"],
    ["Hang right after 'Booting Kernel'", "DTB memory node had size 0 - give explicit RAM ranges"],
    ["init panic, no console", "No /dev/console in initramfs"],
    ["pmOS cannot find its rootfs", "UFS uses 4096-byte sectors - build the image with 4K sectors"],
    ["Phone freezes when modules auto-load", "qcrypto (crypto engine) is TrustZone-owned - blocklist it forever"],
    ["Bluetooth finds nothing", "Antenna-switch rail (PMR735A L7, 2.8 V) must be always-on"],
    ["Display black with correct init", "Missing DCS 0x53 (brightness control) - found in Samsung's panel source"],
    ["Wi-Fi: firmware download 'fails'", "Interrupts lost: mainline pcie-qcom misread iommu-map (5-cell entries) - upstreamable patch written"],
    ["Wi-Fi: chip bootloader crashes", "Fixed by copying the Galaxy Tab S8+ DT (same SoC, same Wi-Fi chip)"],
    ["Wi-Fi: cannot connect", "OPEN: signal ~25 dB weak, router never hears the phone - antenna/RF path"],
    ["Only 6.2 of 8 GB RAM", "Mainline reference reservations (mte_mem) - delete what stock does not reserve"],
    ["USB needs a replug every boot", "No attach event - MAX77705 Type-C driver + role switch"],
    ["'Kernel lacks nftables' at boot", "Enable NF_TABLES (changes struct net: rebuild image AND all modules)"],
], [60*mm, 110*mm]))

story.append(P("Part D - What works today", H1))
story.append(table([
    ["Works", "Partly", "Not yet"],
    ["Boot from internal storage, USB SSH (no replug), display 120 Hz, touch, GPU, Bluetooth, charging, battery %, "
     "6.6 GiB RAM, firewall, on-screen keyboard, Plasma Mobile",
     "Wi-Fi (scans, can't connect), modem (boots, no SIM test), USB host/OTG (untested)",
     "Audio (in progress), calls/SMS, mobile data, sensors, camera, fingerprint, NFC, suspend"],
], [65*mm, 55*mm, 50*mm]))

story.append(P("Part E - Lessons that transfer to any Samsung phone", H1))
story += bullets([
    "Read the phone's <b>stock device tree</b> and <b>Samsung's kernel source for the exact model</b> before guessing.",
    "Find a <b>working port on the same SoC</b> and diff against it - for the S22 the Galaxy Tab S8+ port solved Wi-Fi, USB-C and audio wiring.",
    "Use the phone's <b>own firmware files</b> (GPU zap, ADSP, modem) from its firmware partitions.",
    "One change per test; log everything; re-test 'ruled out' ideas after fixing something more basic.",
    "Get SSH over USB early - most work is then live, without flashing - and add USB-C attach detection right after it (no more replugging).",
])

story.append(P("Part F - Starting checklist for a Galaxy S10+", H1))
story += bullets([
    "Model: SM-G975F (Exynos 9820) is the unlockable one in AU/EU. SM-G975U (Snapdragon, US) is usually locked - stop there.",
    "Do Part A exactly (unlock, Magisk root, EFS backup, TWRP for beyond2lte) before any kernel work.",
    "Check postmarketOS / the Nura wiki for <b>samsung-beyond2lte</b> and any Exynos 9820 mainline work; reuse what exists. "
    "Exynos mainline support is far less complete than Qualcomm's - expect a longer road.",
    "Get Samsung's SM-G975F kernel source and the stock firmware; dump the stock device tree.",
    "uniLoader started life on Exynos phones - check whether it already has an Exynos 9820 board.",
    "First target: a USB shell, then USB-C attach detection ASAP (check which Type-C/PMIC chip the S10+ uses in its stock DT). Then add drivers one at a time.",
])
story.append(Spacer(1, 8))
story.append(P("Files in the S22 project folder: notes/boot-log.md (every attempt), notes/CHANGELOG.md, notes/CHECKLIST-not-sorted.md, "
               "notes/SETUP.md (host setup), notes/flashing-workflow.md, kernel-config/ (DTS, patches, config).", SM))

def footer(c, d):
    c.setFont("Helvetica", 7.5)
    c.setFillColor(colors.grey)
    c.drawString(15*mm, 10*mm, "S22 (SM-S901E) mainline Linux field guide")
    c.drawRightString(195*mm, 10*mm, "page %d" % d.page)

doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=15*mm, rightMargin=15*mm, topMargin=14*mm, bottomMargin=16*mm,
                        title="Mainline Linux on a Samsung Galaxy phone", author="S22 port notes")
doc.build(story, onFirstPage=footer, onLaterPages=footer)
print("wrote", OUT)
