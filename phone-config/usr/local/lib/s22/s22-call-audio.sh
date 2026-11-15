#!/bin/sh
# SPDX-License-Identifier: MIT
# Monitor ModemManager Call StateChanged D-Bus signals to switch ALSA UCM
# between Voice Call (Earpiece + DMIC) and HiFi, and keep AudioReach graphs active.

set -u

trap 'pkill -P $$ 2>/dev/null || true; exit 0' INT TERM

CARD_NAME="Samsung-Galaxy-S22"
ACTIVE_CALL=0

start_call_audio() {
	[ "$ACTIVE_CALL" -eq 1 ] && return
	ACTIVE_CALL=1
	alsaucm -c "$CARD_NAME" set _verb "Voice Call" set _enadev "Earpiece" set _enadev "Mic" 2>/dev/null || true
	if command -v pw-loopback >/dev/null 2>&1; then
		pw-loopback -m '[FL FR]' --capture-props='media.class=Audio/Sink node.name=s22_call_rx_keepalive' >/dev/null 2>&1 &
		pw-loopback -m '[FL FR]' --playback-props='media.class=Audio/Source node.name=s22_call_tx_keepalive' >/dev/null 2>&1 &
	fi
}

stop_call_audio() {
	[ "$ACTIVE_CALL" -eq 0 ] && return
	ACTIVE_CALL=0
	pkill -f 's22_call_.*_keepalive' 2>/dev/null || true
	alsaucm -c "$CARD_NAME" set _verb "HiFi" 2>/dev/null || true
}

dbus-monitor --system "type='signal',interface='org.freedesktop.ModemManager1.Call',member='StateChanged'" |
while read -r line; do
	state="$(printf '%s
' "$line" | awk '/int32/ { print $2 }')"
	case "$state" in
		1|2|3|4)
			# Dialing / RingingOut / RingingIn / Active
			start_call_audio
			;;
		7)
			# Terminated
			stop_call_audio
			;;
	esac
done
