import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// Übertragen: Ausgabe des Players an einen Empfänger im Netz senden
// (DLNA, Chromecast, AirPlay, Lumen-TV-App, Browser)
Popup {
    id: dlg
    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, parent ? parent.width - 40 : 720)
    height: Math.min(660, parent ? parent.height - 40 : 660)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: "#b0000000" }
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.line }

    onOpened: Cast.openDialog()
    onClosed: Cast.closeDialog()

    function typeLabel(type) {
        return type === "dlna" ? "DLNA" : type === "chromecast" ? "Chromecast" : type === "airplay" ? "AirPlay"
             : type === "tv" ? qsTr("Lumen TV") : type
    }
    function stateText() {
        switch (Cast.state) {
        case "starting": return qsTr("Sendestrom wird vorbereitet …")
        case "connecting": return qsTr("Verbindung zu „%1“ …").arg(Cast.deviceName)
        case "playing": return qsTr("Überträgt an „%1“").arg(Cast.deviceName)
        case "error": return Cast.message
        default: return ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text { Layout.fillWidth: true; text: qsTr("Übertragen"); color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("Bild und Ton des Players an einen Fernseher oder Empfänger im selben Netz senden. Gesteuert wird weiter hier.")
                }
            }
            IconButton { iconName: "refresh"; tip: qsTr("Erneut suchen"); onClicked: Cast.refresh() }
            IconButton { iconName: "close"; tip: qsTr("Schließen"); onClicked: dlg.close(); Layout.alignment: Qt.AlignTop }
        }

        // Zustand der laufenden Übertragung
        Rectangle {
            Layout.fillWidth: true
            visible: Cast.state !== "idle"
            implicitHeight: stateRow.implicitHeight + 20
            radius: Theme.radiusSmall
            color: Cast.state === "error" ? Qt.rgba(Theme.bad.r, Theme.bad.g, Theme.bad.b, 0.10) : Theme.accentSoft
            border.color: Cast.state === "error" ? Theme.bad : Theme.accent
            RowLayout {
                id: stateRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                BusyIndicator {
                    running: Cast.state === "starting" || Cast.state === "connecting"
                    visible: running
                    implicitWidth: 22; implicitHeight: 22
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 13
                    text: dlg.stateText()
                }
                Button {
                    visible: Cast.active
                    text: qsTr("Beenden")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    palette.windowText: Theme.accent
                    onClicked: Cast.stop()
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !Cast.available
            wrapMode: Text.WordWrap
            color: Theme.warn
            font.pixelSize: 12
            text: qsTr("Dieser Build kann nicht übertragen (H.264-/AAC-Encoder oder Lumens libmpv fehlt).")
        }

        SectionLabel { text: qsTr("Empfänger") }

        ScrollView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: list.availableWidth
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    visible: Cast.devices.length === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 12
                    text: qsTr("Noch kein Empfänger gefunden. Fernseher und Empfänger müssen eingeschaltet und im selben Netz sein.")
                }
                Repeater {
                    model: Cast.devices
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property bool current: Cast.deviceId === modelData.id && Cast.state !== "idle" && Cast.state !== "error"
                        Layout.fillWidth: true
                        implicitHeight: 52
                        radius: Theme.radiusSmall
                        color: current ? Theme.accentSoft : rowMouse.containsMouse ? Theme.raised : "transparent"
                        border.color: current ? Theme.accent : "transparent"
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: Cast.available
                            onClicked: row.current ? Cast.stop() : Cast.start(row.modelData.id)
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 10
                            Chip { text: dlg.typeLabel(row.modelData.type); tint: row.modelData.type === "tv" ? Theme.good : Theme.accent }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text { Layout.fillWidth: true; text: row.modelData.name; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                                Text {
                                    Layout.fillWidth: true
                                    text: [row.modelData.model, row.modelData.address].filter(s => s && s.length).join("  ·  ")
                                    color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight
                                }
                            }
                            Chip { text: row.modelData.needsPairing ? qsTr("verlangt Kopplung") : ""; tint: Theme.warn }
                            IconButton {
                                visible: row.modelData.manual
                                iconName: "close"; size: 28; iconSize: 14
                                tip: qsTr("Eintrag entfernen")
                                onClicked: Cast.removeDevice(row.modelData.id)
                            }
                            Text {
                                text: row.current ? qsTr("Beenden") : qsTr("Übertragen")
                                color: Theme.accent
                                font.pixelSize: 12
                                visible: Cast.available
                            }
                        }
                    }
                }
            }
        }

        // Gerät von Hand eintragen
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Select {
                id: manualType
                Layout.preferredWidth: 150
                model: [{ value: "chromecast", text: "Chromecast" }, { value: "dlna", text: "DLNA" }, { value: "airplay", text: "AirPlay" }]
                textRole: "text"
                valueRole: "value"
            }
            TextField {
                id: manualAddress
                Layout.fillWidth: true
                placeholderText: manualType.currentValue === "dlna" ? qsTr("Adresse der Gerätebeschreibung, z. B. http://192.168.1.30:49152/description.xml")
                                                                    : qsTr("IP-Adresse des Geräts, z. B. 192.168.1.30")
                color: Theme.text
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle { color: Theme.raised; radius: Theme.radiusSmall; border.color: Theme.line }
                onAccepted: addButton.clicked()
            }
            Button {
                id: addButton
                text: qsTr("Hinzufügen")
                flat: true
                focusPolicy: Qt.NoFocus
                palette.windowText: Theme.accent
                onClicked: if (Cast.addDevice(manualType.currentValue, manualAddress.text)) manualAddress.clear()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        // Lumen TV / Browser
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            textFormat: Text.StyledText
            text: Cast.addresses.length
                  ? qsTr("Lumen-TV-App oder Browser am Fernseher: ") + "<b>" + Cast.addresses.join("</b>  ·  <b>") + "</b>"
                  : qsTr("Der Empfang für Lumen-TV-Apps ist nicht aktiv (Netzwerk-Port belegt?).")
        }
        Toggle {
            Layout.fillWidth: true
            label: qsTr("Für Lumen-TV-Apps erreichbar bleiben")
            hint: qsTr("Sonst nur, solange dieses Fenster offen ist oder übertragen wird")
            checked: Cast.alwaysListen
            onToggled: Cast.alwaysListen = checked
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("Qualität"); color: Theme.textDim; font.pixelSize: 12 }
            Select {
                Layout.preferredWidth: 110
                enabled: !Cast.active
                model: [{ value: 1080, text: "1080p" }, { value: 720, text: "720p" }]
                textRole: "text"; valueRole: "value"
                currentIndex: Cast.height === 720 ? 1 : 0
                onActivated: Cast.height = currentValue
            }
            Select {
                Layout.preferredWidth: 140
                enabled: !Cast.active
                model: [{ value: 30, text: qsTr("bis 30 fps") }, { value: 60, text: qsTr("bis 60 fps") }]
                textRole: "text"; valueRole: "value"
                currentIndex: Cast.fps === 60 ? 1 : 0
                onActivated: Cast.fps = currentValue
            }
            Select {
                Layout.preferredWidth: 120
                enabled: !Cast.active
                model: [{ value: 4000, text: "4 Mbit/s" }, { value: 8000, text: "8 Mbit/s" }, { value: 12000, text: "12 Mbit/s" }, { value: 20000, text: "20 Mbit/s" }]
                textRole: "text"; valueRole: "value"
                currentIndex: Cast.bitrate <= 4000 ? 0 : Cast.bitrate <= 8000 ? 1 : Cast.bitrate <= 12000 ? 2 : 3
                onActivated: Cast.bitrate = currentValue
            }
            Item { Layout.fillWidth: true }
            Button {
                visible: Cast.hasSystemDisplays()
                text: Qt.platform.os === "osx" ? qsTr("AirPlay-Bildschirm …") : qsTr("Drahtloser Bildschirm (Miracast) …")
                flat: true
                focusPolicy: Qt.NoFocus
                palette.windowText: Theme.accent
                onClicked: Cast.openSystemDisplays()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Öffnet die Einstellung des Systems. Der Empfänger erscheint danach als Bildschirm, den Lumen wie jeden anderen bespielt.")
            }
        }
    }
}
