// SPDX-FileCopyrightText: 2026 Florian RICHER <florian.richer@protonmail.com>
// SPDX-License-Identifier: LGPL-2.0-or-later

import QtQuick

import org.kde.plasma.private.mobileshell.quicksettingsplugin as QS
import org.kde.plasma.private.mobileshell as MobileShell

QS.QuickSetting {
    id: root
    text: i18nc("@action:button", "Dark Mode")
    status: MobileShell.DarkModeControl.darkMode ? i18nc("@info:status", "On") : i18nc("@info:status", "Off")
    icon: "lighttable"

    enabled: MobileShell.DarkModeControl.darkMode

    property bool toggling: false
    property string pendingScheme: ""

    Timer {
        id: applyTimer
        interval: 60
        repeat: false
        onTriggered: {
            if (root.pendingScheme.length > 0) {
                MobileShell.ShellUtil.executeCommand("plasma-apply-colorscheme " + root.pendingScheme);
                root.pendingScheme = "";
            }
        }
    }

    Timer {
        id: toggleCooldown
        interval: 800
        repeat: false
        onTriggered: root.toggling = false
    }

    Connections {
        target: MobileShell.DarkModeControl
        function onDarkModeChanged(): void {
            root.toggling = false
            toggleCooldown.stop()
        }
    }

    function toggle(): void {
        if (root.toggling) {
            return;
        }
        root.toggling = true;
        toggleCooldown.restart();
        const nextDark = !MobileShell.DarkModeControl.darkMode;
        root.pendingScheme = nextDark ? "BreezeDark" : "BreezeLight";
        applyTimer.restart();
    }
}
