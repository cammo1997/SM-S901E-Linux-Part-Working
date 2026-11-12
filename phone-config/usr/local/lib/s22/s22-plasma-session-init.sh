#!/bin/sh
# SPDX-License-Identifier: MIT
# Configure Plasma Mobile on Galaxy S22 (1080x2340):
#  1. UI scale 2.5x + 120Hz preferred mode on DSI-1
#  2. Bottom Navigation Bar (Back / Home / Overview) enabled
#  3. PowerDevil screen-off timeouts so locked screen powers off completely
#  4. S22 AOD & Game Mode QuickSettings tiles registered if custom list exists

set -u

MARKER="$HOME/.config/s22-plasma-mobile-v2.done"

# Wait up to 20s for KWin Wayland & PlasmaShell D-Bus
for _ in $(seq 1 40); do
	if command -v qdbus6 >/dev/null 2>&1 &&
	   qdbus6 org.kde.KWin /KWin >/dev/null 2>&1 &&
	   qdbus6 org.kde.plasmashell /PlasmaShell >/dev/null 2>&1; then
		break
	fi
	sleep 0.5
done

# 1. Enable Navigation Bar (Back / Home / Overview) & load bottom taskpanel if missing
if command -v kwriteconfig6 >/dev/null 2>&1; then
	if [ ! -f "$MARKER" ]; then
		kwriteconfig6 --file plasmamobilerc --group General --key navigationPanelEnabled true
		kwriteconfig6 --file plasmamobilerc --group General --key gesturePanelEnabled false
		kwriteconfig6 --file kwinrc --group Compositing --key Backend OpenGL
	fi
fi

if command -v qdbus6 >/dev/null 2>&1; then
	qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript '
		let allPanels = panels();
		let hasTaskPanel = false;
		for (let i = 0; i < allPanels.length; i++) {
			if (allPanels[i].type === "org.kde.plasma.mobile.taskpanel") {
				hasTaskPanel = true;
			}
		}
		if (!hasTaskPanel) {
			loadTemplate("org.kde.plasma.mobile.defaultNavigationPanel");
		}
	' >/dev/null 2>&1 || true
fi

# 2. Apply 2.5x UI scale & 120Hz on DSI-1 + PowerDevil DPMS screen-off on first v2 setup
if [ ! -f "$MARKER" ]; then
	if command -v kscreen-doctor >/dev/null 2>&1; then
		kscreen-doctor output.DSI-1.mode.1080x2340@120 output.DSI-1.scale.2.5 >/dev/null 2>&1 || \
		kscreen-doctor output.DSI-1.scale.2.5 >/dev/null 2>&1 || true
	fi

	if command -v kwriteconfig6 >/dev/null 2>&1; then
		for grp in AC Battery LowBattery; do
			kwriteconfig6 --file powerdevilrc --group "$grp" --group Display --key TurnOffDisplayWhenIdle true
			kwriteconfig6 --file powerdevilrc --group "$grp" --group Display --key TurnOffDisplayIdleTimeoutWhenLockedSec 4
		done
		kwriteconfig6 --file powerdevilrc --group AC --group Display --key TurnOffDisplayIdleTimeoutSec 60
		kwriteconfig6 --file powerdevilrc --group Battery --group Display --key TurnOffDisplayIdleTimeoutSec 35
		kwriteconfig6 --file powerdevilrc --group LowBattery --group Display --key TurnOffDisplayIdleTimeoutSec 20

		# Ensure S22 AOD & Game Mode QuickSettings appear if user already has a saved list
		if grep -q '^enabledQuickSettings=' "$HOME/.config/plasmamobilerc" 2>/dev/null; then
			cur_qs="$(sed -n 's/^enabledQuickSettings=//p' "$HOME/.config/plasmamobilerc" | head -n1)"
			for tile in org.kde.plasma.quicksetting.s22aod org.kde.plasma.quicksetting.s22gamemode; do
				case ",$cur_qs," in
					*,"$tile",*) ;;
					*) cur_qs="${cur_qs:+$cur_qs,}$tile" ;;
				esac
			done
			kwriteconfig6 --file plasmamobilerc --group QuickSettings --key enabledQuickSettings "$cur_qs"
		fi
	fi

	mkdir -p "$HOME/.config"
	date > "$MARKER"
fi

exit 0
