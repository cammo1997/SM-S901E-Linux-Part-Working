#!/bin/sh
# Ensure KWin uses hardware Adreno 730 OpenGL ES / GBM compositing even if
# booted with split card1 (DPU KMS) + card0 (Adreno GPU) prior to reboot.
if [ -e /dev/dri/card1 ] && [ -e /dev/dri/card0 ]; then
    export KWIN_DRM_DEVICES="/dev/dri/card1:/dev/dri/card0"
fi
export KWIN_COMPOSE=O2
