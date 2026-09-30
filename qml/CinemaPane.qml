import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Lumen.Core

// Kino: DCP-Kompositionen, Schlüssel (KDM/Zertifikat), Kinoton, Vorführprogramm
ScrollView {
    id: pane
    contentWidth: availableWidth
    clip: true

    function localPath(url) {
        return decodeURIComponent(url.toString().replace(/^file:\/{2,3}/, Qt.platform.os === "windows" ? "" : "/"))
    }
    function keyTint(state) {
        return state === "valid" || state === "open" ? Theme.good : state === "notyet" ? Theme.warn : Theme.bad
    }

    FolderDialog {
        id: dcpDialog
        title: qsTr("DCP-Ordner öffnen (mit ASSETMAP)")
        onAccepted: Dcp.open(pane.localPath(selectedFolder), false)
    }
    FileDialog {
        id: kdmDialog
        title: qsTr("KDM laden")
        nameFilters: [qsTr("KDM (*.xml)"), qsTr("Alle Dateien (*)")]
        fileMode: FileDialog.OpenFiles
        onAccepted: selectedFiles.forEach(f => Dcp.loadKdm(f))
    }
    FileDialog {
        id: keysDialog
        title: qsTr("Inhaltsschlüssel importieren (Key-ID  Schlüssel je Zeile)")
        nameFilters: [qsTr("Text (*.txt *.keys)"), qsTr("Alle Dateien (*)")]
        onAccepted: Dcp.importKeys(selectedFile)
    }
    FileDialog {
        id: exportDialog
        property bool chain: false
        title: chain ? qsTr("Zertifikatskette speichern") : qsTr("Leaf-Zertifikat speichern")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pem"
        currentFile: "file:///" + (chain ? "lumen-chain.pem" : "lumen-leaf.pem")
        nameFilters: [qsTr("PEM-Zertifikat (*.pem)")]
        onAccepted: Dcp.exportCertificate(selectedFile, chain)
    }
    FileDialog {
        id: certImport
        property url cert
        title: cert.toString() ? qsTr("Privaten Schlüssel (PEM) wählen") : qsTr("Leaf-Zertifikat (PEM) wählen")
        nameFilters: [qsTr("PEM (*.pem *.key *.crt)"), qsTr("Alle Dateien (*)")]
        onAccepted: {
            if (!cert.toString()) {
                cert = selectedFile
                Qt.callLater(open)
            } else {
                Dcp.importIdentity(cert, selectedFile)
                cert = ""
            }
        }
    }

    ColumnLayout {
        width: pane.availableWidth
        spacing: 10

        // ---------------- DCP ----------------
        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: qsTr("Digital Cinema Package"); Layout.fillWidth: true }
            Button { text: qsTr("DCP öffnen …"); flat: true; palette.windowText: Theme.accent; onClicked: dcpDialog.open() }
        }
        Text {
            Layout.fillWidth: true
            visible: Dcp.status.length > 0
            text: Dcp.status
            wrapMode: Text.WordWrap
            color: Dcp.error ? Theme.bad : Theme.textDim
            font.pixelSize: 12
        }
        Text {
            Layout.fillWidth: true
            visible: !Dcp.root && !Dcp.busy
            text: qsTr("Kein DCP geladen. Ordner mit ASSETMAP öffnen – auch Kino-Festplatten werden automatisch in der Laufwerksliste angezeigt.")
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 12
        }
        Repeater {
            model: Dcp.cpls
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: cplCol.implicitHeight + 20
                radius: Theme.radiusSmall
                color: modelData.playing ? Theme.accentSoft : Theme.raised
                border.color: modelData.playing ? Theme.accent : "transparent"
                ColumnLayout {
                    id: cplCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Text { text: Theme.time(modelData.duration); color: Theme.textDim; font.family: Theme.mono; font.pixelSize: 12 }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 5
                        Chip { text: modelData.contentKind || "" }
                        Chip { text: modelData.standard }
                        Chip { text: modelData.resolution ? modelData.resolution + (modelData.width ? " " + modelData.width + "×" + modelData.height : "") : "" }
                        Chip { text: modelData.editRate ? modelData.editRate + qsTr(" fps") : "" }
                        Chip { text: modelData.channels ? modelData.channels + (modelData.channels === 1 ? qsTr(" Kanal") : qsTr(" Kanäle")) : "" }
                        Chip { text: modelData.stereo ? "3D" : ""; tint: Theme.accent }
                        Chip { text: modelData.atmos ? (Dcp.iabAvailable ? qsTr("Atmos/IAB → ") + Dcp.iabLayout : qsTr("Atmos (nicht dekodiert)")) : ""
                               tint: Dcp.iabAvailable ? Theme.accent : Theme.textDim }
                        Chip { text: modelData.subtitles ? qsTr("UT ") + (modelData.subtitleLanguages || []).join(", ") : "" }
                        Chip { text: modelData.reels + qsTr(" Rolle(n)") }
                        Chip { text: modelData.keyText; tint: pane.keyTint(modelData.keyState); filled: modelData.keyState === "valid" }
                    }
                    RowLayout {
                        Button {
                            text: modelData.playing ? qsTr("Neu starten") : qsTr("Abspielen")
                            enabled: modelData.playable
                            highlighted: true
                            palette.highlight: Theme.accent
                            palette.highlightedText: Theme.bg
                            onClicked: Dcp.play(modelData.index)
                        }
                        Button {
                            text: qsTr("Ins Programm")
                            flat: true
                            palette.windowText: Theme.textDim
                            onClicked: Player.queueAdd(Dcp.root, modelData.title, modelData.index)
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: Dcp.verifying ? qsTr("Prüfe ") + Math.round(Dcp.verifyProgress * 100) + " %" : qsTr("Prüfen")
                            flat: true
                            palette.windowText: Theme.textDim
                            onClicked: Dcp.verifying ? Dcp.cancelVerify() : Dcp.verify(modelData.index)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (modelData.markers || []).length > 0
                        text: qsTr("Marker: ") + modelData.markers.map(m => m.label + " " + Theme.time(m.time)).join(" · ")
                        color: Theme.textFaint
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !!Dcp.verifyResult.checked || !!(Dcp.verifyResult.failed || []).length
            text: Dcp.verifyResult.ok ? qsTr("✓ Prüfsummen in Ordnung (") + Dcp.verifyResult.checked + qsTr(" Dateien)")
                                      : qsTr("✗ Fehler: ") + (Dcp.verifyResult.failed || []).join(", ")
            color: Dcp.verifyResult.ok ? Theme.good : Theme.bad
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // ---------------- Kinoton ----------------
        SectionLabel { text: qsTr("Kinoton") }
        ValueSlider {
            Layout.fillWidth: true
            label: qsTr("Fader (Kinoprozessor, 7,0 = Referenz) · ") + (Dcp.fader >= 7 ? "+" : "") + (Dcp.fader >= 4 ? ((Dcp.fader - 7) * 10 / 3) : (-10 + (Dcp.fader - 4) * 20)).toFixed(1) + qsTr(" dB")
            from: 3; to: 10; stepSize: 0.1; decimals: 1; defaultValue: 7
            value: Dcp.fader
            onMoved: v => Dcp.fader = v
        }
        Select {
            Layout.fillWidth: true
            model: [
                { value: "auto", text: qsTr("Kanäle: automatisch (16 Kanäle → 7.1 DS, sonst 5.1)") },
                { value: "51", text: qsTr("5.1 (L R C LFE Ls Rs)") },
                { value: "71", text: qsTr("7.1 (mit hinteren Surrounds)") },
                { value: "hi", text: qsTr("HI – Spur für Hörgeschädigte (Kanal 7)") },
                { value: "vi", text: qsTr("VI-N – Audiodeskription (Kanal 8)") }
            ]
            textRole: "text"; valueRole: "value"
            currentIndex: indexFor(Dcp.audioRoute)
            onActivated: Dcp.audioRoute = currentValue
        }
        Select {
            Layout.fillWidth: true
            visible: Dcp.iabAvailable
            model: Dcp.iabLayouts.map(l => ({ value: l.value, text: qsTr("Atmos/IAB-Ausgabe: ") + l.text }))
            textRole: "text"; valueRole: "value"
            currentIndex: indexFor(Dcp.iabLayout)
            onActivated: Dcp.iabLayout = currentValue
        }
        Select {
            Layout.fillWidth: true
            model: [
                { value: "auto", text: qsTr("JPEG 2000: automatisch (Auflösungsstufe nach Ausgabe, Echtzeit-Fallback)") },
                { value: "full", text: qsTr("JPEG 2000: volle Auflösung") },
                { value: "half", text: qsTr("JPEG 2000: halbe Auflösung (4K → 2K, schnell)") },
                { value: "quarter", text: qsTr("JPEG 2000: Viertel (schwache CPUs)") }
            ]
            textRole: "text"; valueRole: "value"
            currentIndex: indexFor(Dcp.decodeMode)
            onActivated: Dcp.decodeMode = currentValue
        }

        // ---------------- Schlüssel ----------------
        SectionLabel { text: qsTr("Schlüssel (KDM)") }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: idCol.implicitHeight + 20
            radius: Theme.radiusSmall
            color: Theme.raised
            ColumnLayout {
                id: idCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 5
                Text {
                    Layout.fillWidth: true
                    text: !Dcp.cryptoAvailable ? qsTr("Ohne OpenSSL gebaut – nur unverschlüsselte DCPs")
                        : Dcp.identity.valid ? qsTr("Zertifikat dieses Players") : qsTr("Noch kein Zertifikat – für verschlüsselte DCPs erzeugen")
                    color: Dcp.identity.valid ? Theme.text : Theme.warn
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!Dcp.identity.valid
                    text: qsTr("Seriennummer ") + (Dcp.identity.serial || "") + qsTr("\nThumbprint ") + (Dcp.identity.thumbprint || "")
                          + qsTr("\ndnQualifier ") + (Dcp.identity.dnQualifier || "")
                    color: Theme.textDim
                    font.family: Theme.mono
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    enabled: Dcp.cryptoAvailable
                    Button { text: Dcp.identity.valid ? qsTr("Neu erzeugen") : qsTr("Zertifikat erzeugen"); flat: true; palette.windowText: Theme.accent
                             onClicked: Dcp.identity.valid ? confirmNew.open() : Dcp.createIdentity(qsTr("Lumen")) }
                    Button { text: qsTr("Leaf exportieren"); flat: true; visible: !!Dcp.identity.valid; palette.windowText: Theme.accent
                             onClicked: { exportDialog.chain = false; exportDialog.open() } }
                    Button { text: qsTr("Kette exportieren"); flat: true; visible: !!Dcp.identity.valid; palette.windowText: Theme.textDim
                             onClicked: { exportDialog.chain = true; exportDialog.open() } }
                    Button { text: qsTr("Importieren …"); flat: true; palette.windowText: Theme.textDim; onClicked: { certImport.cert = ""; certImport.open() } }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Das Leaf-Zertifikat an den Verleih bzw. KDM-Ersteller senden. KDMs für dieses Zertifikat hier laden; sie werden gespeichert und bei jedem Start ausgepackt.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }
        Dialog {
            id: confirmNew
            title: qsTr("Neues Zertifikat erzeugen?")
            modal: true
            anchors.centerIn: Overlay.overlay
            standardButtons: Dialog.Ok | Dialog.Cancel
            Text { text: qsTr("Bereits ausgestellte KDMs passen danach nicht mehr."); color: Theme.text }
            background: Rectangle { color: Theme.panel; border.color: Theme.line; radius: Theme.radius }
            onAccepted: Dcp.createIdentity(qsTr("Lumen"))
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("KDM laden …"); enabled: !!Dcp.identity.valid; flat: true; palette.windowText: Theme.accent; onClicked: kdmDialog.open() }
            Button { text: qsTr("Schlüsseldatei …"); flat: true; palette.windowText: Theme.textDim; onClicked: keysDialog.open() }
            Item { Layout.fillWidth: true }
            Text { text: Dcp.keyCount + qsTr(" Schlüssel"); color: Theme.textFaint; font.pixelSize: 11 }
        }
        Repeater {
            model: Dcp.kdms
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 6
                Rectangle { width: 8; height: 8; radius: 4; color: modelData.valid ? Theme.good : Theme.bad }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { Layout.fillWidth: true; text: modelData.title || modelData.file; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.error ? modelData.error
                              : modelData.keys + qsTr(" Schlüssel · bis ") + Qt.formatDateTime(modelData.notAfter, qsTr("dd.MM.yyyy HH:mm"))
                                + " · " + modelData.signatureText
                                + (modelData.cplLoaded ? "" : qsTr(" · CPL nicht geladen"))
                        color: modelData.error ? Theme.bad : Theme.textDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                IconButton { iconName: "stop"; size: 26; iconSize: 12; tip: qsTr("KDM entfernen"); onClicked: Dcp.removeKdm(index) }
            }
        }

        // ---------------- Programm ----------------
        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: qsTr("Vorführprogramm"); Layout.fillWidth: true }
            Button {
                text: qsTr("Aktuelles hinzufügen")
                flat: true
                enabled: !Player.idle && Player.sourceKind !== "dcp"
                palette.windowText: Theme.textDim
                onClicked: Player.queueAdd(Player.device || Player.path, Player.mediaTitle, -1)
            }
        }
        Repeater {
            model: Player.queue
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: (index + 1) + "."
                    color: Player.queueActive && Player.queueIndex === index ? Theme.accent : Theme.textFaint
                    font.pixelSize: 12
                    Layout.preferredWidth: 20
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.label + "  ·  " + modelData.kind
                    color: Player.queueActive && Player.queueIndex === index ? Theme.accent : Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }
                IconButton { iconName: "up"; size: 24; iconSize: 12; tip: qsTr("Nach oben"); onClicked: Player.queueMove(index, -1) }
                IconButton { iconName: "down"; size: 24; iconSize: 12; tip: qsTr("Nach unten"); onClicked: Player.queueMove(index, 1) }
                IconButton { iconName: "stop"; size: 24; iconSize: 11; tip: qsTr("Entfernen"); onClicked: Player.queueRemove(index) }
            }
        }
        RowLayout {
            visible: Player.queue.length > 0
            Button {
                text: Player.queueActive ? qsTr("Programm läuft …") : qsTr("Programm starten")
                enabled: !Player.queueActive
                highlighted: true
                palette.highlight: Theme.accent
                palette.highlightedText: Theme.bg
                onClicked: Player.queueStart(0)
            }
            Button { text: qsTr("Anhalten"); flat: true; visible: Player.queueActive; palette.windowText: Theme.textDim; onClicked: Player.queueStop() }
            Button { text: qsTr("Leeren"); flat: true; palette.windowText: Theme.textDim; onClicked: Player.queueClear() }
        }
        Text {
            Layout.fillWidth: true
            visible: Player.queue.length === 0
            text: qsTr("Werbung, Trailer, Hauptfilm … nacheinander abspielen: CPLs oder laufende Quellen hinzufügen.")
            color: Theme.textFaint
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        Item { implicitHeight: 8 }
    }
}
