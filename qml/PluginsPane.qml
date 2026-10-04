import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// Plugins: installierte Erweiterungen, aktivieren/deaktivieren, Aktionen
ScrollView {
    id: pane
    contentWidth: availableWidth
    clip: true
    // Store-Index beim ersten Anzeigen laden
    Component.onCompleted: Store.refresh()

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
                        text: card.modelData.warning || ""
                        color: Theme.warn
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

        // ---------------- Plugin-Store ----------------
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            SectionLabel { text: qsTr("Plugin-Store"); Layout.fillWidth: true }
            Text { visible: Store.busy; text: qsTr("Lädt …"); color: Theme.textFaint; font.pixelSize: 11 }
            Button { text: qsTr("Aktualisieren"); flat: true; palette.windowText: Theme.accent; onClicked: Store.refresh() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 11
            text: qsTr("Plugins sind ausführbarer Code: nur aus Quellen installieren, denen du vertraust. Installierte Plugins sind zunächst deaktiviert.")
        }
        Text {
            visible: Store.status.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: Store.status
            color: Theme.textDim
            font.pixelSize: 12
        }
        // Quellen: offizielles Repository + eigene
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: Store.sources
                delegate: Rectangle {
                    id: srcChip
                    required property var modelData
                    height: 26
                    width: srcRow.implicitWidth + 16
                    radius: 13
                    color: Theme.raised
                    border.color: srcChip.modelData.error ? Theme.bad : Theme.line
                    Row {
                        id: srcRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: (srcChip.modelData.name || srcChip.modelData.source)
                                  + (srcChip.modelData.error ? " – " + srcChip.modelData.error : " (" + srcChip.modelData.count + ")")
                            color: srcChip.modelData.error ? Theme.bad : Theme.textDim
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: !srcChip.modelData.builtin
                            text: "×"
                            color: Theme.textFaint
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                            TapHandler { onTapped: Store.removeSource(srcChip.modelData.source) }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: sourceField
                Layout.fillWidth: true
                placeholderText: qsTr("Eigene Quelle: owner/repo, GitHub-URL oder URL/Ordner mit index.json")
                color: Theme.text
                font.pixelSize: 12
                background: Rectangle { color: Theme.raised; radius: Theme.radiusSmall; border.color: Theme.line }
                onAccepted: if (Store.addSource(text)) text = ""
            }
            Button { text: qsTr("Hinzufügen"); flat: true; palette.windowText: Theme.accent; onClicked: if (Store.addSource(sourceField.text)) sourceField.text = "" }
        }
        Repeater {
            model: Store.available
            delegate: Rectangle {
                id: storeCard
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: storeCol.implicitHeight + 20
                radius: Theme.radiusSmall
                color: Theme.raised
                ColumnLayout {
                    id: storeCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: storeCard.modelData.name; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.maximumWidth: storeCard.width * 0.5 }
                        Text { text: storeCard.modelData.version; color: Theme.textFaint; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Button {
                            visible: storeCard.modelData.supported && (!storeCard.modelData.installed || storeCard.modelData.update)
                            text: storeCard.modelData.update ? qsTr("Aktualisieren") : qsTr("Installieren")
                            flat: true; enabled: !Store.busy; palette.windowText: Theme.accent
                            onClicked: Store.install(storeCard.modelData.source, storeCard.modelData.id)
                        }
                        Button {
                            visible: storeCard.modelData.storeInstalled
                            text: qsTr("Entfernen")
                            flat: true; palette.windowText: Theme.textDim
                            onClicked: Store.uninstall(storeCard.modelData.id)
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Chip { text: storeCard.modelData.installed ? qsTr("installiert ") + storeCard.modelData.installed : ""; tint: Theme.good }
                        Chip { text: storeCard.modelData.update ? qsTr("Update verfügbar") : ""; tint: Theme.warn }
                        Chip { text: storeCard.modelData.native ? qsTr("Nativ") : "" }
                        Chip { text: storeCard.modelData.supported ? "" : qsTr("nicht für dieses System"); tint: Theme.bad }
                        Chip { text: storeCard.modelData.sourceName; tint: Theme.textFaint }
                    }
                    Text {
                        visible: text.length > 0
                        text: storeCard.modelData.description
                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
