#!/bin/sh
# SPDX-License-Identifier: MIT
# Bring up Galaxy S22 (SM-S901E, SM8450) MPSS cellular modem, IPA v5.1 data path,
# Dual-SIM QMI UIM provisioning session, and QMI GNSS engine at boot.

set -u

LOG=/var/log/s22-modem.log
log() {
	printf '%s [s22-modem] %s
' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" | tee -a "$LOG"
}

# 1. Verify read-only EFS copies exist before starting MPSS
for f in modem_fs1 modem_fs2 modem_fsg; do
	if [ ! -s "/var/lib/rmtfs/$f" ]; then
		log "ERROR: /var/lib/rmtfs/$f missing; refusing to boot modem without EFS copies"
		exit 1
	fi
done

# 2. Load kernel drivers for MPSS, IPA v5.1, RMNET and WWAN
for mod in rmtfs_mem qcom_pd_mapper qcom_sysmon qcom_q6v5_pas qrtr qrtr_smd qrtr_tun ipa rmnet; do
	modprobe "$mod" 2>/dev/null || true
done

# 2b. Ensure rmtfs and tqftpserv are running before MPSS boots
udevadm settle --timeout=5 2>/dev/null || true
if command -v systemctl >/dev/null 2>&1; then
	systemctl start tqftpserv.service rmtfs.service 2>/dev/null || true
	sleep 1
fi

# 3. Locate the MPSS remoteproc instance dynamically
MODEM_RPROC=""
for r in /sys/class/remoteproc/remoteproc*; do
	[ -f "$r/name" ] || continue
	if [ "$(cat "$r/name" 2>/dev/null)" = "modem" ]; then
		MODEM_RPROC="$r"
		break
	fi
done

if [ -z "$MODEM_RPROC" ]; then
	log "ERROR: modem remoteproc not found in /sys/class/remoteproc"
	exit 1
fi

STATE="$(cat "$MODEM_RPROC/state" 2>/dev/null || echo unknown)"
if [ "$STATE" != "running" ]; then
	log "Starting MPSS on $(basename "$MODEM_RPROC") (was $STATE)..."
	echo start > "$MODEM_RPROC/state"
fi

# 4. Wait up to 20s for MPSS running + QRTR QMI DMS/UIM services
for _ in $(seq 1 40); do
	if [ "$(cat "$MODEM_RPROC/state" 2>/dev/null)" = "running" ] &&
	   command -v qmicli >/dev/null 2>&1 &&
	   qmicli -d qrtr://0 --dms-get-operating-mode >/dev/null 2>&1; then
		break
	fi
	sleep 0.5
done

STATE="$(cat "$MODEM_RPROC/state" 2>/dev/null || echo unknown)"
log "MPSS state: $STATE"
[ "$STATE" = "running" ] || exit 1

if command -v qmicli >/dev/null 2>&1; then
	# 5. Ensure modem operating mode is online
	OPMODE="$(qmicli -d qrtr://0 --dms-get-operating-mode 2>/dev/null | sed -n "s/.*Mode: '\([^']*\)'.*/\1/p" | head -n1)"
	log "QMI DMS operating mode: ${OPMODE:-unknown}"
	if [ -n "$OPMODE" ] && [ "$OPMODE" != "online" ]; then
		qmicli -d qrtr://0 --dms-set-operating-mode=online >/dev/null 2>&1 || true
	fi

	# 6. Dual-SIM UIM auto-provisioning (Slot 1 / Slot 2 USIM -> primary-gw-provisioning)
	CARD_STATUS="$(qmicli -d qrtr://0 --uim-get-card-status 2>/dev/null || true)"
	if printf '%s
' "$CARD_STATUS" | grep -q "Primary GW:.*session doesn't exist"; then
		PROVISION_ARGS="$(printf '%s
' "$CARD_STATUS" | awk '
			/Slot \[[0-9]+\]:/ {
				gsub(/[^0-9]/, "", $2);
				slot = $2;
				present = 0;
			}
			/Card state:.*present/ { present = 1 }
			present && /Application \[[0-9]+\]:/ { in_app = 1; is_usim = 0; aid = "" }
			present && in_app && /Application type:.*usim/ { is_usim = 1 }
			present && in_app && is_usim && /Application ID:/ {
				getline;
				gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0);
				aid = $0;
				if (slot != "" && aid != "") {
					printf "slot=%s,activate=yes,session-type=primary-gw-provisioning,aid=%s
", slot, aid;
					exit;
				}
			}
		')"
		if [ -n "$PROVISION_ARGS" ]; then
			log "Activating SIM UIM provisioning session: $PROVISION_ARGS"
			qmicli -d qrtr://0 --uim-change-provisioning-session="$PROVISION_ARGS" >>"$LOG" 2>&1 || true
		else
			log "No unprovisioned USIM detected yet (insert SIM card to register)"
		fi
	fi

	# 7. Unlock QMI GNSS location engine for ModemManager Location interface
	qmicli -d qrtr://0 --loc-set-engine-lock=mt >/dev/null 2>&1 || true
	qmicli -d qrtr://0 --loc-set-nmea-types=all >/dev/null 2>&1 || true
fi

# 8. Retrigger udev on IPA and WWAN devices so ModemManager binds immediately
udevadm trigger --action=add --subsystem-match=net --subsystem-match=wwan 2>/dev/null || true
log "Modem + IPA bring-up complete (netdevs: $(ls /sys/class/net | tr '
' ' '))"
exit 0
