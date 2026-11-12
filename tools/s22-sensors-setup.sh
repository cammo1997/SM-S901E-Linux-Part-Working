#!/bin/sh
# SPDX-License-Identifier: MIT
# Populate /usr/share/qcom/sm8450/Samsung/r0q/ for hexagonrpcd-sdsp from an
# extracted read-only copy of the phone's persist/sensors/registry directory.
set -e

REG_SRC=${1:?usage: $0 /path/to/persist-sensors/registry}
HEXROOT=/usr/share/qcom/sm8450/Samsung/r0q

mkdir -p "$HEXROOT/socinfo" "$HEXROOT/sensors/registry" "$HEXROOT/sensors/config"
cp "$REG_SRC/registry/"* "$HEXROOT/sensors/registry/"
[ -f "$REG_SRC/sns_reg_version" ] && cp "$REG_SRC/sns_reg_version" "$HEXROOT/sensors/sns_reg_version"

printf "MTP\n"    > "$HEXROOT/socinfo/hw_platform"
printf "0\n"      > "$HEXROOT/socinfo/platform_subtype"
printf "0\n"      > "$HEXROOT/socinfo/platform_subtype_id"
printf "131074\n" > "$HEXROOT/socinfo/platform_version"
printf "457\n"    > "$HEXROOT/socinfo/soc_id"
printf "12\n"     > "$HEXROOT/socinfo/ssc_hw_rev"

cat > "$HEXROOT/sensors/sns_reg.conf" << 'CONF'
version=1
file=hw_platform=/sys/devices/soc0/hw_platform
file=platform_subtype=/sys/devices/soc0/platform_subtype
file=platform_subtype_id=/sys/devices/soc0/platform_subtype_id
file=platform_version=/sys/devices/soc0/platform_version
file=soc_id=/sys/devices/soc0/soc_id
file=revision=/sys/devices/soc0/ssc_hw_rev
file=output=/mnt/vendor/persist/sensors/registry/registry
CONF

chmod -R a+rX "$HEXROOT"
echo "Installed S22 SLPI sensor registry under $HEXROOT"
