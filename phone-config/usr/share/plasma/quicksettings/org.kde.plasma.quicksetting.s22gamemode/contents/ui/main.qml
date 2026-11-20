// SPDX-License-Identifier: LGPL-2.0-or-later
import QtQuick
import org.kde.plasma.private.mobileshell as MobileShell
import org.kde.plasma.private.mobileshell.quicksettingsplugin as QS

QS.QuickSetting {
    id: root
    text: "Modo Jogo (90Hz)"
    icon: "input-gaming"
    status: gameOn ? "Ativo (Jogo)" : "Din?mico 120/60Hz"
    enabled: gameOn
    settingsCommand: "/usr/local/bin/s22-display-settings"

    property bool gameOn: false

    function refreshState() {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", "file:///run/s22-display-state.json", true);
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE && xhr.responseText) {
                try {
                    const st = JSON.parse(xhr.responseText);
                    root.gameOn = (st.game === 1);
                } catch (e) {}
            }
        };
        xhr.send();
    }

    function toggle() {
        const next = !root.gameOn;
        root.gameOn = next;
        MobileShell.ShellUtil.executeCommand("/usr/local/bin/s22-display-ctl game " + (next ? "on" : "off"));
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
