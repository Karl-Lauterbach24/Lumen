import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// Streaming: Links aller Art und Medienserver (Jellyfin, Emby, Plex)
ScrollView {
    id: pane
    contentWidth: availableWidth
    clip: true

    Settings {
        id: streamSettings
        category: "streams"
        property string history: "[]"
    }
    function historyList() { try { return JSON.parse(streamSettings.history) } catch (e) { return [] } }
    function playLink(url) {
        url = url.trim()
        if (!url.length) return
        const h = historyList().filter(u => u !== url)
        h.unshift(url)
        streamSettings.history = JSON.stringify(h.slice(0, 12))
        Player.openStream(url, "", 0)
    }
    function fmt(s) {
        if (!(s > 0)) return ""
        const m = Math.round(s / 60)
        return m >= 60 ? Math.floor(m / 60) + " h " + (m % 60) + " min" : m + " min"
    }

    ColumnLayout {
        width: pane.availableWidth
        spacing: 10

        // ---------------- Link ----------------
        SectionLabel { text: qsTr("Link öffnen") }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: linkField
                Layout.fillWidth: true
                placeholderText: qsTr("https://…, .m3u8, .mpd, rtsp://, rtmp://, srt://, udp://, smb://")
                color: Theme.text
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle { color: Theme.raised; radius: Theme.radiusSmall; border.color: Theme.line }
                onAccepted: pane.playLink(text)
            }
            Button { text: qsTr("Abspielen"); flat: true; palette.windowText: Theme.accent; onClicked: pane.playLink(linkField.text) }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 11
            text: Player.ytdlAvailable ? qsTr("Webseiten wie YouTube, Vimeo oder Mediatheken werden über yt-dlp geöffnet.")
                                       : qsTr("Für Webseiten wie YouTube oder Mediatheken yt-dlp installieren (neben Lumen oder im PATH).")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: pane.historyList()
                delegate: Button {
                    required property string modelData
                    text: modelData.length > 60 ? modelData.slice(0, 57) + "…" : modelData
                    flat: true
                    palette.windowText: Theme.textDim
                    font.pixelSize: 11
                    onClicked: pane.playLink(modelData)
                }
            }
        }

        // ---------------- Medienserver ----------------
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            SectionLabel { text: qsTr("Medienserver"); Layout.fillWidth: true }
            Text { visible: Servers.busy; text: qsTr("Lädt …"); color: Theme.textFaint; font.pixelSize: 11 }
        }
        Text {
            visible: Servers.status.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: Servers.status
            color: Theme.textDim
            font.pixelSize: 12
        }
        Text {
            visible: Servers.plexPinUrl.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            textFormat: Text.RichText
            text: qsTr("Falls sich kein Browser geöffnet hat:") + " <a href=\"" + Servers.plexPinUrl + "\">app.plex.tv/auth</a>"
            color: Theme.textDim
            font.pixelSize: 11
            onLinkActivated: link => Qt.openUrlExternally(link)
        }

        // Serverliste (ohne geöffneten Server)
        Repeater {
            model: Servers.current ? [] : Servers.servers
            delegate: Rectangle {
                id: srv
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: srvRow.implicitHeight + 16
                radius: Theme.radiusSmall
                color: srvHover.hovered ? Theme.hover : Theme.raised
                HoverHandler { id: srvHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: Servers.openServer(srv.modelData.id) }
                RowLayout {
                    id: srvRow
                    anchors.fill: parent
                    anchors.margins: 8
                    Chip { text: srv.modelData.type === "plex" ? "Plex" : srv.modelData.type === "emby" ? "Emby" : "Jellyfin"; tint: Theme.accent }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { text: srv.modelData.name; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: (srv.modelData.user ? srv.modelData.user + " · " : "") + srv.modelData.url; color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    }
                    Button { text: qsTr("Entfernen"); flat: true; palette.windowText: Theme.textDim; onClicked: Servers.removeServer(srv.modelData.id) }
                }
            }
        }

        // Server hinzufügen
        Rectangle {
            visible: !Servers.current
            Layout.fillWidth: true
            implicitHeight: addCol.implicitHeight + 20
            radius: Theme.radiusSmall
            color: Theme.raised
            ColumnLayout {
                id: addCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                RowLayout {
                    spacing: 4
                    Repeater {
                        model: [{ id: "jellyfin", name: "Jellyfin" }, { id: "emby", name: "Emby" }, { id: "plex", name: "Plex" }]
                        delegate: Button {
                            required property var modelData
                            text: modelData.name
                            checkable: true
                            checked: serverType.value === modelData.id
                            flat: true
                            palette.windowText: checked ? Theme.accent : Theme.textDim
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: parent.checked ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14) : "transparent"
                                border.color: parent.checked ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4) : "transparent"
                            }
                            onClicked: serverType.value = modelData.id
                        }
                    }
                    QtObject { id: serverType; property string value: "jellyfin" }
                }
                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    placeholderText: serverType.value === "plex" ? qsTr("Server-Adresse, z. B. http://192.168.1.10:32400") : qsTr("Server-Adresse, z. B. http://192.168.1.10:8096")
                    color: Theme.text; font.pixelSize: 12; selectByMouse: true
                    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.line }
                }
                RowLayout {
                    visible: serverType.value !== "plex"
                    Layout.fillWidth: true
                    TextField {
                        id: userField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Benutzer")
                        color: Theme.text; font.pixelSize: 12; selectByMouse: true
                        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.line }
                    }
                    TextField {
                        id: passField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Passwort")
                        echoMode: TextInput.Password
                        color: Theme.text; font.pixelSize: 12
                        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.line }
                        onAccepted: connectButton.clicked()
                    }
                }
                TextField {
                    id: tokenField
                    visible: serverType.value === "plex"
                    Layout.fillWidth: true
                    placeholderText: qsTr("X-Plex-Token (optional, sonst „Mit Plex anmelden“)")
                    echoMode: TextInput.Password
                    color: Theme.text; font.pixelSize: 12
                    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.line }
                }
                RowLayout {
                    Button {
                        id: connectButton
                        text: qsTr("Verbinden")
                        flat: true
                        enabled: urlField.text.length > 0 && (serverType.value !== "plex" || tokenField.text.length > 0)
                        palette.windowText: Theme.accent
                        onClicked: {
                            if (serverType.value === "plex") Servers.addPlexServer(urlField.text, tokenField.text)
                            else Servers.addEmbyServer(serverType.value, urlField.text, userField.text, passField.text)
                            passField.text = ""
                        }
                    }
                    Button {
                        visible: serverType.value === "plex"
                        text: Servers.plexPinUrl ? qsTr("Anmeldung abbrechen") : qsTr("Mit Plex anmelden …")
                        flat: true
                        palette.windowText: Theme.accent
                        onClicked: Servers.plexPinUrl ? Servers.cancelPlexSignIn() : Servers.plexSignIn()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: qsTr("Lumen speichert nur das Zugriffstoken des Servers, nie das Passwort. Plex-Anmeldung läuft im Browser.")
                }
            }
        }

        // Geöffneter Server: Navigation
        RowLayout {
            visible: !!Servers.current
            Layout.fillWidth: true
            spacing: 6
            IconButton { iconName: "left"; tip: qsTr("Zurück"); onClicked: Servers.back() }
            Text {
                Layout.fillWidth: true
                text: Servers.path.join("  ›  ")
                color: Theme.text
                font.pixelSize: 13
                elide: Text.ElideLeft
            }
            Button { text: qsTr("Weiterschauen"); flat: true; palette.windowText: Theme.accent; onClicked: Servers.resume() }
        }
        TextField {
            visible: !!Servers.current
            Layout.fillWidth: true
            placeholderText: qsTr("Suchen …")
            color: Theme.text; font.pixelSize: 12; selectByMouse: true
            background: Rectangle { color: Theme.raised; radius: Theme.radiusSmall; border.color: Theme.line }
            onAccepted: Servers.search(text)
        }
        Flow {
            visible: !!Servers.current
            Layout.fillWidth: true
            spacing: 10
            Repeater {
                model: Servers.items
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    width: 128
                    height: poster.height + label.implicitHeight + 12
                    radius: Theme.radiusSmall
                    color: tileHover.hovered ? Theme.hover : "transparent"
                    HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: Servers.openItem(tile.modelData) }
                    Rectangle {
                        id: poster
                        x: 4; y: 4
                        width: parent.width - 8
                        height: width * 1.5
                        radius: Theme.radiusSmall
                        color: Theme.raised
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: tile.modelData.image || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.height: 360
                        }
                        Image {
                            visible: !tile.modelData.image
                            anchors.centerIn: parent
                            source: Theme.icon(tile.modelData.folder ? "folder" : "play")
                            sourceSize: Qt.size(28, 28)
                            opacity: 0.5
                        }
                        Rectangle {
                            visible: tile.modelData.resume > 0 && tile.modelData.duration > 0
                            anchors.bottom: parent.bottom
                            height: 3
                            width: parent.width * Math.min(1, tile.modelData.resume / Math.max(1, tile.modelData.duration))
                            color: Theme.accent
                        }
                    }
                    Column {
                        id: label
                        anchors.top: poster.bottom
                        anchors.topMargin: 4
                        x: 4
                        width: parent.width - 8
                        Text { width: parent.width; text: tile.modelData.title; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                        Text {
                            width: parent.width
                            text: [tile.modelData.subtitle, pane.fmt(tile.modelData.duration)].filter(x => !!x).join(" · ")
                            color: Theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
