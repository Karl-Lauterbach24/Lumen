import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// Hilfe & Info: Version, Lizenz, Verweise und die Tastenkürzel des Steuerfensters (F1)
Popup {
    id: help
    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(760, parent ? parent.width - 40 : 760)
    height: Math.min(640, parent ? parent.height - 40 : 640)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: "#b0000000" }
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.line }

    readonly property string repo: "https://github.com/Karl-Lauterbach24/Lumen"
    readonly property var shortcuts: [
        { keys: qsTr("Leertaste"), text: qsTr("Wiedergabe / Pause") },
        { keys: "←  →", text: qsTr("10 Sekunden zurück / vor") },
        { keys: "Shift + ←  →", text: qsTr("1 Minute zurück / vor") },
        { keys: "↑  ↓", text: qsTr("Lautstärke") },
        { keys: "PgUp  PgDn", text: qsTr("Nächstes / vorheriges Kapitel") },
        { keys: ",  .", text: qsTr("Einzelbild zurück / vor") },
        { keys: "[  ]  ⌫", text: qsTr("Langsamer / schneller / normale Geschwindigkeit") },
        { keys: "F", text: qsTr("Vollbild im Player-Fenster") },
        { keys: "M", text: qsTr("Stumm") },
        { keys: "L", text: qsTr("A-B-Schleife") },
        { keys: "S", text: qsTr("Screenshot") },
        { keys: "I", text: qsTr("Statistik im Bild") },
        { keys: "Home  End", text: qsTr("Disc-Hauptmenü / Pop-up-Menü") },
        { keys: "Ctrl + O", text: qsTr("Datei öffnen") },
        { keys: "Ctrl + E", text: qsTr("Disc auswerfen") },
        { keys: "Ctrl + D", text: qsTr("Tab „Kino“") },
        { keys: "F1", text: qsTr("Diese Hilfe") }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Image {
                source: "qrc:/qt/qml/Lumen/resources/logo/lumen-icon-256.png"
                Layout.preferredWidth: 64; Layout.preferredHeight: 64
                sourceSize: Qt.size(128, 128)
                smooth: true; mipmap: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text { Layout.fillWidth: true; text: qsTr("Lumen"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; text: qsTr("Disc- und Kino-Player") + "  ·  " + qsTr("Version %1").arg(Qt.application.version); color: Theme.textDim; font.pixelSize: 13 }
            }
            IconButton { iconName: "close"; tip: qsTr("Schließen"); onClicked: help.close(); Layout.alignment: Qt.AlignTop }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: qsTr("Freie Software unter der GNU Affero General Public License, Version 3 oder neuer.") + " "
                  + qsTr("Lumen enthält keinen Code zum Umgehen von Kopierschutz.")
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [
                    { text: qsTr("Projektseite"), url: help.repo },
                    { text: qsTr("Neue Versionen"), url: help.repo + "/releases" },
                    { text: qsTr("Plugin-Store"), url: "https://github.com/Karl-Lauterbach24/Lumen-Plugins" },
                    { text: qsTr("Fehler melden"), url: help.repo + "/issues" },
                    { text: qsTr("Lizenzen der Bibliotheken"), url: help.repo + "/blob/main/THIRD_PARTY.md" }
                ]
                delegate: Button {
                    required property var modelData
                    text: modelData.text
                    flat: true
                    focusPolicy: Qt.NoFocus
                    palette.windowText: Theme.accent
                    onClicked: Qt.openUrlExternally(modelData.url)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
        SectionLabel { text: qsTr("Tastenkürzel") }

        ScrollView {
            id: keysView
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            GridLayout {
                width: keysView.availableWidth
                columns: 2
                columnSpacing: 18
                rowSpacing: 6
                Repeater {
                    model: help.shortcuts
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 118
                            implicitHeight: 24
                            radius: 5
                            color: Theme.raised
                            border.color: Theme.line
                            Text {
                                anchors.centerIn: parent
                                text: modelData.keys
                                color: Theme.text
                                font.family: Theme.mono
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.text
                            color: Theme.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 11
            text: qsTr("Dateien, Ordner und Links lassen sich auch ins Fenster ziehen.")
        }
    }
}
