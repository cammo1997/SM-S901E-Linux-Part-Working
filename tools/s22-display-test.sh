#!/bin/sh
# Attempt 13: bring up the real display path (MDSS -> DSI0 -> S6E3FAC panel) by hand.
# dispcc_sm8450 is blacklisted at boot; loading it lets msm bind MDSS/DSI and the panel driver probe.
LOG=/tmp/s22-display-test.log
exec > "$LOG" 2>&1
echo "== before"; ls /sys/class/drm/; cat /sys/class/regulator/*/name | tr '\n' ' '; echo
dmesg -C
modprobe dispcc_sm8450
sleep 10
echo "== modules"; lsmod | grep -E "dispcc|msm|panel|drm_display"
echo "== drm"; ls /sys/class/drm/
for c in /sys/class/drm/card*-*; do echo "$c: $(cat $c/status 2>/dev/null) $(head -1 $c/modes 2>/dev/null)"; done
echo "== backlight"; ls /sys/class/backlight/
echo "== dmesg"; dmesg | grep -iE "msm|dpu|dsi|panel|s6e3fac|mdss|dispcc|drm|simpledrm|backlight|regulator" | head -80
echo "== errors"; dmesg | grep -iE "fail|error|timeout|-110|-22|-517" | head -40
