// SPDX-License-Identifier: LGPL-2.0-or-later
import QtQuick
import org.kde.plasma.private.mobileshell as MobileShell
import org.kde.plasma.private.mobileshell.quicksettingsplugin as QS

QS.QuickSetting {
    id: root
    text: "Always-On Display"
    icon: "preferences-desktop-display"
    status: aodOn ? (aodNits + " nits") : "Desativado"
    enabled: aodOn
    settingsCommand: "/usr/local/bin/s22-display-settings"

    property bool aodOn: false
    property int aodNits: 10

    function refreshState() {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", "file:///run/s22-display-state.json", true);
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE && xhr.responseText) {
                try {
                    const st = JSON.parse(xhr.responseText);
                    root.aodOn = (st.aod === 1);
                    if (st.aod_nits) root.aodNits = st.aod_nits;
                } catch (e) {}
            }
        };
        xhr.send();
    }

    function toggle() {
        const next = !root.aodOn;
        root.aodOn = next;
        MobileShell.ShellUtil.executeCommand("/usr/local/bin/s22-display-ctl aod " + (next ? "on" : "off"));
        syncTimer.restart();
    }

    Timer {
        id: syncTimer
        interval: 600
        repeat: false
        onTriggered: root.refreshState()
    }

    Component.onCompleted: refreshState()
}
