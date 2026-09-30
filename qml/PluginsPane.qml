import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// Plugins: installierte Erweiterungen, aktivieren/deaktivieren, Aktionen
ScrollView {
    id: pane
    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: pane.availableWidth
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: qsTr("Plugins"); Layout.fillWidth: true }
            Button { text: qsTr("Plugin-Ordner öffnen"); flat: true; palette.windowText: Theme.accent; onClicked: Plugins.openUserDir() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 11
            text: qsTr("Plugins erweitern Lumen um Quellen, Schlüssel, Skripte und Funktionen. Lumen enthält selbst keine Umgehung von Kopierschutz – Bibliotheken dafür (z. B. libaacs, libbdplus, libdvdcss) bindet nur ein Plugin ein, das du selbst installierst und aktivierst. Du bist dafür verantwortlich, dass die Nutzung in deinem Land erlaubt ist.")
        }

        Rectangle {
            Layout.fillWidth: true
            visible: Plugins.restartNeeded
            implicitHeight: restartRow.implicitHeight + 16
            radius: Theme.radiusSmall
            color: Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.12)
            RowLayout {
                id: restartRow
                anchors.fill: parent
                anchors.margins: 8
                Text { text: qsTr("Änderungen wirken nach einem Neustart."); color: Theme.warn; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button { text: qsTr("Jetzt neu starten"); flat: true; palette.windowText: Theme.warn; onClicked: Plugins.restartApp() }
            }
        }

        Text {
            visible: Plugins.plugins.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: qsTr("Keine Plugins installiert. Lege einen Plugin-Ordner (mit plugin.json) hier ab:") + "\n" + Plugins.userDir
        }

        Repeater {
            model: Plugins.plugins
            delegate: Rectangle {
                id: card
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: cardCol.implicitHeight + 20
                radius: Theme.radiusSmall
                color: Theme.raised

                ColumnLayout {
                    id: cardCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: card.modelData.name
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.maximumWidth: card.width * 0.5
                        }
                        Text { text: card.modelData.version; color: Theme.textFaint; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Toggle {
                            implicitWidth: 44
                            checked: card.modelData.enabled
                            onToggled: Plugins.setEnabled(card.modelData.id, checked)
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Chip { text: card.modelData.loaded ? qsTr("aktiv") : ""; tint: Theme.good; filled: true }
                        Chip { text: card.modelData.pending ? qsTr("nach Neustart") : ""; tint: Theme.warn }
                        Repeater {
                            model: card.modelData.kinds
                            delegate: Chip { required property string modelData; text: modelData }
                        }
                    }
                    Text {
                        visible: text.length > 0
                        text: card.modelData.description
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: text.length > 0
                        text: card.modelData.error
                        color: Theme.bad
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: text.length > 0
                        text: card.modelData.status
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Flow {
                        visible: card.modelData.actions.length > 0
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: card.modelData.actions
                            delegate: Button {
                                required property var modelData
                                text: modelData.label
                                flat: true
                                palette.windowText: Theme.accent
                                onClicked: Plugins.trigger(card.modelData.id, modelData.id)
                            }
                        }
                    }
                    Text {
                        text: (card.modelData.author ? card.modelData.author + " · " : "") + card.modelData.dir
                        color: Theme.textFaint
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
