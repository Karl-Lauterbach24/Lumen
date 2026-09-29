import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Lumen.Core

// Steuerfenster. Das Bild läuft im separaten Player-Fenster (mpv, nativ).
ApplicationWindow {
    id: win
    width: 1180
    height: 720
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: "Lumen"
    color: Theme.bg
    font.family: Theme.font
    font.pixelSize: 13

    palette {
        window: Theme.bg
        windowText: Theme.text
        base: Theme.raised
        text: Theme.text
        button: Theme.raised
        buttonText: Theme.text
        highlight: Theme.accent
        highlightedText: Theme.bg
        placeholderText: Theme.textFaint
        toolTipBase: Theme.raised
        toolTipText: Theme.text
    }

    Settings {
        id: settings
        category: "ui"
        property bool autoPlay: true
        property bool startWithMenu: true
        property int tab: 0
        property alias width: win.width
        property alias height: win.height
    }

    // ------------------------------------------------------------------
    // Zustand
    // ------------------------------------------------------------------
    property string currentDevice: ""
    readonly property var selectedDrive: driveSelect.currentIndex >= 0 ? Drives.drives[driveSelect.currentIndex] : null
    readonly property var vinfo: Player.videoInfo
    readonly property var ainfo: Player.audioInfo

    // Im Menümodus liefert libbluray Zeit, Kapitel und Spulen
    readonly property bool navMode: Nav.active
    readonly property real curPos: navMode ? Nav.position : Player.position
    readonly property real curDur: navMode ? Nav.duration : Player.duration
    readonly property var curChapters: navMode ? Nav.chapters : Player.chapters
    readonly property int curChapter: navMode ? Nav.chapter : Player.currentChapter
    function seekAbs(s) { if (navMode) Nav.seek(s); else Player.seek(s, false) }
    function seekRel(d) { if (navMode) Nav.seekRelative(d); else Player.seek(d, true) }
    function nextChapter() { if (navMode) Nav.nextChapter(); else Player.nextChapter() }
    function prevChapter() { if (navMode) Nav.prevChapter(); else Player.prevChapter() }
    function setChapter(i) { if (navMode) Nav.setChapter(i); else Player.setChapter(i) }

    function stereoLabel(v) {
        return ({ none: "2D", fp: "HDMI Frame Packing", sbs2l: "Side-by-Side Half", sbsl: "Side-by-Side Full",
                  ab2l: "Top-and-Bottom Half", abl: "Top-and-Bottom Full", irl: "Zeilenverschachtelt",
                  arcd: "Anaglyph" })[v || "none"] || v
    }

    function playDrive(drive, withMenu) {
        if (!drive) return
        currentDevice = drive.path
        Disc.scan(drive.path)
        const menu = withMenu === undefined ? settings.startWithMenu : withMenu
        if (menu && Nav.available) Player.openDiscMenu(drive.path)
        else Player.openDisc(drive.path, "longest")
    }
    function ejectSelected() {
        if (!selectedDrive) return
        if (currentDevice === selectedDrive.path) Player.stop()
        Disc.clear()
        Drives.eject(selectedDrive.device)
    }

    // Disc von außen geöffnet (Kommandozeile, Neustart) -> Gerät übernehmen
    Connections {
        target: Player
        function onMediaChanged() {
            if (Player.isDisc && Player.device) win.currentDevice = Player.device
        }
    }

    Connections {
        target: Drives
        function onDiscInserted(drive) {
            if (settings.autoPlay && Player.idle) win.playDrive(drive)
            else Disc.scan(drive.path)
        }
    }

    FileDialog {
        id: fileDialog
        title: "Datei oder ISO öffnen"
        nameFilters: ["Medien (*.iso *.mkv *.m2ts *.mts *.ts *.mp4 *.mov *.webm *.bdmv)", "Alle Dateien (*)"]
        onAccepted: {
            const p = selectedFile.toString()
            if (/\.iso$/i.test(p)) {
                win.currentDevice = decodeURIComponent(p.replace(/^file:\/{2,3}/, Qt.platform.os === "windows" ? "" : "/"))
                Disc.scan(win.currentDevice)
            }
            Player.openFile(selectedFile)
        }
    }
    FolderDialog {
        id: folderDialog
        title: "Blu-ray-Ordner (mit BDMV) öffnen"
        onAccepted: {
            const path = decodeURIComponent(selectedFolder.toString().replace(/^file:\/{2,3}/, Qt.platform.os === "windows" ? "" : "/"))
            win.currentDevice = path
            Disc.scan(path)
            Player.openDisc(path, "longest")
        }
    }

    ProfileEditor { id: editor }

    // ------------------------------------------------------------------
    // Tastatur (Steuerfenster)
    // ------------------------------------------------------------------
    Shortcut { sequence: "Space"; onActivated: Player.togglePause() }
    // Im Disc-Menü steuern Pfeile/Enter die Menüauswahl
    Shortcut { sequence: "Left"; onActivated: if (!(Nav.menuVisible && Nav.key("left"))) win.seekRel(-10) }
    Shortcut { sequence: "Right"; onActivated: if (!(Nav.menuVisible && Nav.key("right"))) win.seekRel(10) }
    Shortcut { sequence: "Shift+Left"; onActivated: win.seekRel(-60) }
    Shortcut { sequence: "Shift+Right"; onActivated: win.seekRel(60) }
    Shortcut { sequence: "Up"; onActivated: if (!(Nav.menuVisible && Nav.key("up"))) Player.setVolume(Math.min(Player.volumeMax, Player.volume + 5)) }
    Shortcut { sequence: "Down"; onActivated: if (!(Nav.menuVisible && Nav.key("down"))) Player.setVolume(Math.max(0, Player.volume - 5)) }
    Shortcut { sequence: "Return"; enabled: Nav.menuVisible; onActivated: Nav.key("enter") }
    Shortcut { sequence: "Home"; enabled: Nav.active; onActivated: Nav.key("menu") }
    Shortcut { sequence: "End"; enabled: Nav.active; onActivated: Nav.key("popup") }
    Shortcut { sequence: "PgUp"; onActivated: win.nextChapter() }
    Shortcut { sequence: "PgDown"; onActivated: win.prevChapter() }
    Shortcut { sequence: "."; onActivated: Player.frameStep() }
    Shortcut { sequence: ","; onActivated: Player.frameBackStep() }
    Shortcut { sequence: "F"; onActivated: Player.toggleFullscreen() }
    Shortcut { sequence: "M"; onActivated: Player.setMuted(!Player.muted) }
    Shortcut { sequence: "L"; onActivated: Player.cycleAbLoop() }
    Shortcut { sequence: "I"; onActivated: Player.toggleStats() }
    Shortcut { sequence: "S"; onActivated: Player.screenshot() }
    Shortcut { sequence: "["; onActivated: Player.setSpeed(Math.max(0.25, Player.speed - 0.1)) }
    Shortcut { sequence: "]"; onActivated: Player.setSpeed(Math.min(4, Player.speed + 0.1)) }
    Shortcut { sequence: "Backspace"; onActivated: Player.setSpeed(1) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequence: "Ctrl+E"; onActivated: win.ejectSelected() }

    // ------------------------------------------------------------------
    // Layout
    // ------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ============ Kopfzeile ============
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: Theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 14
                spacing: 8

                Row {
                    spacing: 8
                    Layout.rightMargin: 18
                    Rectangle { width: 10; height: 10; radius: 5; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "LUMEN"; color: Theme.text; font.pixelSize: 15; font.weight: Font.Bold; font.letterSpacing: 4 }
                }

                Select {
                    id: driveSelect
                    Layout.preferredWidth: 300
                    model: Drives.drives
                    textRole: "title"
                    popupWidth: 380
                    placeholder: Drives.scanning ? "Suche Laufwerke …" : "Kein Laufwerk gefunden"
                    onActivated: if (win.selectedDrive && win.selectedDrive.isBluray) Disc.scan(win.selectedDrive.path)
                }
                IconButton {
                    iconName: "disc"
                    tip: "Hauptfilm direkt abspielen"
                    enabled: !!win.selectedDrive && !!win.selectedDrive.hasDisc
                    onClicked: win.playDrive(win.selectedDrive, false)
                }
                IconButton {
                    iconName: "menu"
                    visible: Nav.available
                    tip: "Mit Disc-Menü starten"
                    enabled: !!win.selectedDrive && !!win.selectedDrive.hasDisc
                    onClicked: win.playDrive(win.selectedDrive, true)
                }
                IconButton { iconName: "eject"; tip: "Auswerfen (Strg+E)"; enabled: !!win.selectedDrive; onClicked: win.ejectSelected() }
                IconButton {
                    iconName: "folder"
                    tip: "Datei / ISO / BDMV-Ordner öffnen"
                    onClicked: openMenu.popup()
                    Menu {
                        id: openMenu
                        MenuItem { text: "Datei oder ISO …"; onTriggered: fileDialog.open() }
                        MenuItem { text: "Blu-ray-Ordner (BDMV) …"; onTriggered: folderDialog.open() }
                        background: Rectangle { implicitWidth: 220; color: Theme.raised; border.color: Theme.line; radius: Theme.radiusSmall }
                    }
                }

                Item { Layout.fillWidth: true }

                Text { text: "Ausgabe"; color: Theme.textFaint; font.pixelSize: 12 }
                Select {
                    id: profileSelect
                    Layout.preferredWidth: 300
                    model: Profiles.profiles
                    textRole: "name"
                    valueRole: "id"
                    popupWidth: 340
                    currentIndex: indexFor(Profiles.currentId)
                    onActivated: Profiles.currentId = currentValue
                }
                IconButton { iconName: "tune"; tip: "Profil bearbeiten"; onClicked: editor.openFor(Profiles.current) }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        }

        // ============ Inhalt ============
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---------- Linke Seite: Jetzt läuft + Transport ----------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 28
                spacing: 18

                // Jetzt läuft
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: !Player.idle

                    SectionLabel { text: Player.isDisc ? "Blu-ray" : "Datei" }
                    Text {
                        Layout.fillWidth: true
                        text: (Player.isDisc && Disc.info.discName) ? Disc.info.discName
                            : (Player.isDisc && win.selectedDrive && win.selectedDrive.label) ? win.selectedDrive.label
                            : Player.mediaTitle
                        color: Theme.text
                        font.pixelSize: 28
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: {
                            const parts = []
                            if (win.navMode) {
                                parts.push(!Nav.menuMode ? "Titel" : Nav.menuVisible ? "Disc-Menü" : "Menümodus")
                                if (Nav.playlist >= 0) parts.push(("0000" + Nav.playlist).slice(-5) + ".mpls")
                            } else if (Player.isDisc && Player.currentTitle >= 0) {
                                parts.push("Titel " + (Player.currentTitle + 1))
                            }
                            if (win.curChapters.length) parts.push("Kapitel " + (win.curChapter + 1) + " / " + win.curChapters.length)
                            if (!Player.isDisc) parts.push(Player.path)
                            return parts.join("   ·   ")
                        }
                        color: Theme.textDim
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                    }
                    Flow {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 6
                        Chip { text: win.vinfo.range || ""; tint: win.vinfo.range === "SDR" ? Theme.textDim : Theme.hdr; filled: !!win.vinfo.range && win.vinfo.range !== "SDR" }
                        Chip { text: win.vinfo.height >= 2000 ? "4K" : win.vinfo.height >= 1000 ? "1080p" : win.vinfo.height > 0 ? win.vinfo.height + "p" : "" }
                        Chip { text: win.vinfo.codec || "" }
                        Chip { text: win.vinfo.fps > 0 ? win.vinfo.fps.toFixed(3) + " fps" : "" }
                        Chip { text: (win.ainfo.codec || "") + (win.ainfo.channels ? " " + win.ainfo.channels : "") }
                        Chip { text: win.ainfo.passthrough ? "BITSTREAM" : ""; tint: Theme.good }
                        Chip {
                            text: Player.mvcActive ? "3D MVC → " + win.stereoLabel(Profiles.current.stereoOut)
                                : Player.stereoInput !== "none" ? "3D"
                                : Disc.info.has3d ? "3D-Disc (2D)" : ""
                            tint: Theme.accent
                            filled: Player.mvcActive
                        }
                        Chip { text: Player.buffering ? "Puffert …" : ""; tint: Theme.warn }
                        Chip { text: Player.speed !== 1 ? Player.speed.toFixed(2) + "×" : ""; tint: Theme.accent }
                        Chip { text: Player.embedded ? "Eingebettet · SDR" : ""; tint: Theme.textDim }
                    }

                    // Fernbedienung für Disc-Menüs
                    RowLayout {
                        visible: win.navMode && Nav.menuMode
                        Layout.topMargin: 10
                        spacing: 18

                        Grid {
                            columns: 3
                            spacing: 4
                            Item { width: 36; height: 36 }
                            IconButton { iconName: "up"; tip: "Hoch"; onClicked: Nav.key("up") }
                            Item { width: 36; height: 36 }
                            IconButton { iconName: "left"; tip: "Links"; onClicked: Nav.key("left") }
                            Button {
                                width: 36; height: 36
                                text: "OK"
                                focusPolicy: Qt.NoFocus
                                font.pixelSize: 11; font.weight: Font.Bold
                                onClicked: Nav.key("enter")
                                contentItem: Text { text: parent.text; color: Theme.bg; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 18; color: parent.down ? "#c9ccd6" : Theme.text }
                            }
                            IconButton { iconName: "right"; tip: "Rechts"; onClicked: Nav.key("right") }
                            Item { width: 36; height: 36 }
                            IconButton { iconName: "down"; tip: "Runter"; onClicked: Nav.key("down") }
                            Item { width: 36; height: 36 }
                        }
                        ColumnLayout {
                            spacing: 6
                            Button {
                                text: "Hauptmenü"
                                flat: true
                                palette.windowText: Theme.text
                                onClicked: Nav.key("menu")
                            }
                            Button {
                                text: "Pop-up-Menü"
                                flat: true
                                enabled: Nav.popupAvailable
                                palette.windowText: Theme.text
                                onClicked: Nav.key("popup")
                            }
                            Text {
                                text: "Pfeile/Enter/Maus funktionieren auch im Player-Fenster · Pos1 = Hauptmenü · Ende = Pop-up"
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: win.vinfo.summary || ""
                        color: Theme.textFaint
                        font.family: Theme.mono
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                // Leerlauf: Discs direkt anbieten
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: Player.idle
                    spacing: 14
                    SectionLabel { text: "Bereit" }
                    Text {
                        text: Drives.drives.some(d => d.isBluray) ? "Disc erkannt – bereit zur Wiedergabe" : "Disc einlegen oder Datei öffnen"
                        color: Theme.text
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: Drives.drives.filter(d => d.hasDisc)
                            delegate: Rectangle {
                                required property var modelData
                                width: 260; height: 76
                                radius: Theme.radius
                                color: tile.hovered ? Theme.hover : Theme.raised
                                border.color: Theme.line
                                HoverHandler { id: tile; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: win.playDrive(parent.modelData) }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 12
                                    Image { source: Theme.icon("disc"); sourceSize: Qt.size(28, 28); opacity: 0.8 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { Layout.fillWidth: true; text: modelData.label || modelData.device; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                        Text {
                                            Layout.fillWidth: true
                                            text: (modelData.isUhd ? "UHD Blu-ray" : modelData.isBluray ? "Blu-ray" : "Disc") + (modelData.is3d ? " · 3D" : "") + (modelData.hardware ? " · " + modelData.hardware : "")
                                            color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Toggle {
                        label: "Discs beim Einlegen automatisch abspielen"
                        checked: settings.autoPlay
                        onToggled: settings.autoPlay = checked
                        implicitWidth: 360
                    }
                    Toggle {
                        visible: Nav.available
                        label: "Mit Disc-Menü starten"
                        hint: "Aus: Hauptfilm direkt abspielen"
                        checked: settings.startWithMenu
                        onToggled: settings.startWithMenu = checked
                        implicitWidth: 360
                    }
                }

                Item { Layout.fillHeight: true }

                // Transport
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: transport.implicitHeight + 36
                    radius: 16
                    color: Theme.panel
                    border.color: Theme.line

                    ColumnLayout {
                        id: transport
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 8

                        SeekBar {
                            Layout.fillWidth: true
                            position: win.curPos
                            duration: win.curDur
                            chapters: win.curChapters
                            loopA: win.navMode ? -1 : Player.loopA
                            loopB: win.navMode ? -1 : Player.loopB
                            cacheSeconds: win.navMode ? 0 : Player.cacheSeconds
                            onSeekRequested: s => win.seekAbs(s)
                            // Scrubbing nur im Direktmodus – im Menümodus springt libbluray beim Loslassen
                            onScrub: s => { if (!win.navMode) Player.command(["seek", s.toFixed(2), "absolute+keyframes"]) }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: Theme.time(win.curPos); color: Theme.textDim; font.family: Theme.mono; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: win.curChapter >= 0 && win.curChapters[win.curChapter] ? win.curChapters[win.curChapter].title : ""
                                color: Theme.textFaint; font.pixelSize: 12
                            }
                            Item { Layout.fillWidth: true }
                            Text { text: win.curDur > 0 ? "−" + Theme.time(win.curDur - win.curPos) : "--:--"; color: Theme.textDim; font.family: Theme.mono; font.pixelSize: 12 }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            // links: Präzision
                            RowLayout {
                                spacing: 2
                                Layout.preferredWidth: 220
                                IconButton { iconName: "frame-back"; tip: "Einzelbild zurück (,)"; onClicked: Player.frameBackStep() }
                                IconButton { iconName: "frame-fwd"; tip: "Einzelbild vor (.)"; onClicked: Player.frameStep() }
                                IconButton {
                                    iconName: "loop"
                                    active: Player.loopA >= 0
                                    tip: Player.loopB >= 0 ? "A-B-Schleife aufheben (L)" : Player.loopA >= 0 ? "Punkt B setzen (L)" : "Punkt A setzen (L)"
                                    onClicked: Player.cycleAbLoop()
                                }
                                IconButton { iconName: "camera"; tip: "Screenshot (S)"; onClicked: Player.screenshot() }
                            }

                            Item { Layout.fillWidth: true }

                            // Mitte: Transport
                            IconButton { iconName: "prev"; tip: "Vorheriges Kapitel (Bild ab)"; onClicked: win.prevChapter() }
                            IconButton { iconName: "rewind"; tip: "−10 s (←)"; onClicked: win.seekRel(-10) }
                            IconButton {
                                iconName: Player.paused || Player.idle ? "play" : "pause"
                                primary: true
                                size: 54
                                iconSize: 26
                                Layout.leftMargin: 8
                                Layout.rightMargin: 8
                                tip: "Wiedergabe / Pause (Leertaste)"
                                onClicked: {
                                    if (Player.idle && win.selectedDrive && win.selectedDrive.hasDisc) win.playDrive(win.selectedDrive)
                                    else Player.togglePause()
                                }
                            }
                            IconButton { iconName: "forward"; tip: "+10 s (→)"; onClicked: win.seekRel(10) }
                            IconButton { iconName: "next"; tip: "Nächstes Kapitel (Bild auf)"; onClicked: win.nextChapter() }
                            IconButton { iconName: "stop"; tip: "Stopp"; enabled: !Player.idle; onClicked: Player.stop() }

                            Item { Layout.fillWidth: true }

                            // rechts: Lautstärke + Fenster
                            RowLayout {
                                spacing: 2
                                Layout.preferredWidth: 220
                                IconButton { iconName: Player.muted || Player.volume === 0 ? "mute" : "volume"; tip: "Stumm (M)"; onClicked: Player.setMuted(!Player.muted) }
                                Slider {
                                    id: vol
                                    Layout.preferredWidth: 96
                                    from: 0; to: Player.volumeMax
                                    focusPolicy: Qt.NoFocus
                                    onMoved: Player.setVolume(value)
                                    Binding on value { value: Player.volume; when: !vol.pressed }
                                    background: Rectangle {
                                        x: vol.leftPadding; y: vol.topPadding + vol.availableHeight / 2 - 2
                                        width: vol.availableWidth; height: 4; radius: 2; color: Theme.line
                                        Rectangle { width: vol.visualPosition * parent.width; height: 4; radius: 2; color: Player.muted ? Theme.textFaint : Theme.text }
                                    }
                                    handle: Rectangle {
                                        x: vol.leftPadding + vol.visualPosition * (vol.availableWidth - width)
                                        y: vol.topPadding + vol.availableHeight / 2 - 6
                                        width: 12; height: 12; radius: 6; color: Theme.text
                                    }
                                }
                                IconButton { iconName: "fullscreen"; active: Player.fullscreen; tip: "Vollbild im Player-Fenster (F)"; onClicked: Player.toggleFullscreen() }
                                IconButton { iconName: "info"; tip: "Statistik im Bild (I)"; onClicked: Player.toggleStats() }
                            }
                        }
                    }
                }
            }

            // ---------- Rechte Seite: Panels ----------
            Rectangle {
                Layout.preferredWidth: 400
                Layout.fillHeight: true
                color: Theme.panel
                Rectangle { width: 1; height: parent.height; color: Theme.line }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 1
                    spacing: 0

                    // Tabs
                    Row {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.topMargin: 10
                        spacing: 2
                        Repeater {
                            model: ["Titel", "Kapitel", "Ton", "Untertitel", "Bild", "Ausgabe"]
                            delegate: AbstractButton {
                                id: tabBtn
                                required property string modelData
                                required property int index
                                readonly property bool current: settings.tab === index
                                focusPolicy: Qt.NoFocus
                                implicitWidth: tabLabel.implicitWidth + 20
                                implicitHeight: 36
                                onClicked: settings.tab = index
                                contentItem: Text {
                                    id: tabLabel
                                    text: tabBtn.modelData
                                    color: tabBtn.current ? Theme.text : tabBtn.hovered ? Theme.textDim : Theme.textFaint
                                    font.pixelSize: 13
                                    font.weight: tabBtn.current ? Font.DemiBold : Font.Normal
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: parent.width - 16; height: 2; radius: 1
                                    color: tabBtn.current ? Theme.accent : "transparent"
                                }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 12
                        currentIndex: settings.tab

                        // ---- Titel ----
                        ColumnLayout {
                            spacing: 10

                            Rectangle {
                                Layout.fillWidth: true
                                visible: Disc.available && (Disc.busy || Disc.info.device !== undefined)
                                implicitHeight: discStatus.implicitHeight + 20
                                radius: Theme.radiusSmall
                                color: Theme.raised
                                ColumnLayout {
                                    id: discStatus
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 4
                                    readonly property var i: Disc.info
                                    Text {
                                        visible: Disc.busy
                                        text: "Lese Disc-Struktur …"
                                        color: Theme.textDim; font.pixelSize: 12
                                    }
                                    Text {
                                        visible: !Disc.busy && !!discStatus.i.error
                                        text: discStatus.i.error || ""
                                        color: Theme.bad; font.pixelSize: 12
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    }
                                    Flow {
                                        visible: !Disc.busy && !discStatus.i.error
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Chip {
                                            text: !discStatus.i.aacsDetected ? "Kein AACS"
                                                : discStatus.i.aacsHandled ? "AACS · extern gelöst"
                                                : !discStatus.i.aacsLibrary ? "AACS · keine externe Bibliothek"
                                                : "AACS · Fehler " + discStatus.i.aacsError
                                            tint: !discStatus.i.aacsDetected || discStatus.i.aacsHandled ? Theme.good : Theme.bad
                                        }
                                        Chip {
                                            text: discStatus.i.bdplusDetected ? (discStatus.i.bdplusHandled ? "BD+ · extern gelöst" : "BD+ · nicht gelöst") : ""
                                            tint: discStatus.i.bdplusHandled ? Theme.good : Theme.warn
                                        }
                                        Chip { text: discStatus.i.bdjDetected ? "BD-J-Menü" : ""; tint: Theme.textDim }
                                        Chip { text: discStatus.i.has3d ? "3D-Inhalt" : ""; tint: Theme.accent }
                                    }
                                    Text {
                                        visible: !Disc.busy && !!discStatus.i.aacsDetected && !discStatus.i.aacsHandled
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: Theme.textFaint; font.pixelSize: 11
                                        text: "Lumen entschlüsselt nicht selbst. Die Disc muss über ein LibreDrive-Laufwerk mit einer vom System bereitgestellten AACS-Bibliothek lesbar sein."
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                SectionLabel { text: "Titel / Playlists"; Layout.fillWidth: true }
                                Button {
                                    text: "Disc-Menü"
                                    flat: true
                                    visible: Nav.available && win.currentDevice.length > 0
                                    palette.windowText: Theme.accent
                                    onClicked: Player.openDiscMenu(win.currentDevice)
                                }
                                Button {
                                    text: "Hauptfilm"
                                    flat: true
                                    visible: win.currentDevice.length > 0
                                    palette.windowText: Theme.accent
                                    onClicked: Player.openDisc(win.currentDevice, "longest")
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: Nav.status.length > 0
                                text: Nav.status
                                wrapMode: Text.WordWrap
                                color: Nav.active ? Theme.good : Theme.warn
                                font.pixelSize: 12
                            }
                            SelectList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                emptyText: Nav.available ? "Keine Disc geladen.\nStart mit Disc-Menü oder Titel hier direkt wählen."
                                                         : "Keine Disc geladen.\nOhne libbluray gebaut – Titel werden direkt gewählt."
                                model: {
                                    const t = Disc.info.titles
                                    if (Disc.available && t && t.length)
                                        return t.map(x => ({
                                            label: "Titel " + (x.index + 1) + (x.main ? "   ★ Hauptfilm" : ""),
                                            detail: [x.video, x.audio, x.chapters + " Kap.", ("0000" + x.playlist).slice(-5) + ".mpls"].filter(s => s).join("  ·  "),
                                            trailing: Theme.time(x.duration),
                                            selected: (win.navMode && Nav.playlist === x.playlist) || Player.path === "bd://mpls/" + x.playlist || (Player.path === "bd://longest" && x.main),
                                            playlist: x.playlist
                                        }))
                                    return Player.titles.map(x => ({
                                        label: "Titel " + (x.index + 1),
                                        trailing: x.duration > 0 ? Theme.time(x.duration) : "",
                                        selected: x.index === Player.currentTitle,
                                        index: x.index
                                    }))
                                }
                                onPicked: (i, item) => {
                                    if (item.playlist !== undefined) Player.openPlaylist(win.currentDevice, item.playlist)
                                    else Player.setTitle(item.index)
                                }
                            }
                        }

                        // ---- Kapitel ----
                        SelectList {
                            emptyText: "Keine Kapitel"
                            model: win.curChapters.map(c => ({
                                label: c.title,
                                trailing: Theme.time(c.time),
                                selected: c.index === win.curChapter
                            }))
                            onPicked: i => win.setChapter(i)
                        }

                        // ---- Ton ----
                        ColumnLayout {
                            spacing: 10
                            SectionLabel { text: "Tonspur" }
                            SelectList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                emptyText: "Keine Tonspuren"
                                model: Player.audioTracks.map(t => ({ label: t.label, selected: t.id === Player.audioId, id: t.id }))
                                onPicked: (i, item) => Player.setAudioId(item.id)
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: !!win.ainfo.format
                                text: win.ainfo.passthrough ? "Bitstream an Receiver: " + win.ainfo.format.replace("spdif-", "").toUpperCase()
                                                            : "PCM " + (win.ainfo.channels || "") + (win.ainfo.samplerate ? " · " + (win.ainfo.samplerate / 1000) + " kHz" : "")
                                color: win.ainfo.passthrough ? Theme.good : Theme.textDim
                                font.pixelSize: 12
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: "Audio-Verzögerung"
                                from: -2; to: 2; stepSize: 0.01; decimals: 2; unit: " s"
                                value: Player.audioDelay
                                onMoved: v => Player.setAudioDelay(v)
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: "Geschwindigkeit"
                                from: 0.25; to: 2; stepSize: 0.05; decimals: 2; unit: "×"; defaultValue: 1
                                value: Player.speed
                                onMoved: v => Player.setSpeed(v)
                            }
                        }

                        // ---- Untertitel ----
                        ColumnLayout {
                            id: subsPane
                            spacing: 10
                            property real subScale: 1
                            property real subPos: 100
                            SectionLabel { text: "Untertitel" }
                            SelectList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: [{ label: "Aus", selected: Player.subtitleId === 0, id: 0 }]
                                       .concat(Player.subtitleTracks.map(t => ({ label: t.label, selected: t.id === Player.subtitleId, id: t.id })))
                                onPicked: (i, item) => Player.setSubtitleId(item.id)
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                visible: Nav.mvcActive
                                label: "3D-Tiefe (Untertitel & Menü)"
                                from: -40; to: 40; stepSize: 1; unit: " px"
                                value: Nav.subtitleDepth
                                onMoved: v => Nav.subtitleDepth = Math.round(v)
                            }
                            Toggle {
                                Layout.fillWidth: true
                                visible: !Nav.mvcActive
                                label: "Nur erzwungene Untertitel"
                                hint: "Zeigt nur als 'forced' markierte Einblendungen (PGS)"
                                onToggled: Player.setOption("sub-forced-events-only", checked)
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: "Verzögerung"
                                from: -5; to: 5; stepSize: 0.05; decimals: 2; unit: " s"
                                value: Player.subDelay
                                onMoved: v => Player.setSubDelay(v)
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: "Größe"
                                from: 0.5; to: 2; stepSize: 0.05; decimals: 2; unit: "×"; defaultValue: 1
                                value: subsPane.subScale
                                onMoved: v => { subsPane.subScale = v; Player.setOption("sub-scale", v) }
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: "Position (für 2.39:1-Leinwände nach oben ziehen)"
                                from: 50; to: 100; stepSize: 1; unit: " %"; defaultValue: 100
                                value: subsPane.subPos
                                onMoved: v => { subsPane.subPos = v; Player.setOption("sub-pos", Math.round(v)) }
                            }
                        }

                        // ---- Bild ----
                        ScrollView {
                            id: picPane
                            contentWidth: availableWidth
                            clip: true
                            property var eq: ({ brightness: 0, contrast: 0, saturation: 0, gamma: 0 })
                            property real zoom: 0
                            property real panscan: 0
                            function setEq(k, v) {
                                const e = Object.assign({}, eq); e[k] = v; eq = e
                                Player.setOption(k, Math.round(v))
                            }

                            ColumnLayout {
                                width: picPane.availableWidth
                                spacing: 10

                                SectionLabel { text: "Seitenverhältnis & Ausschnitt" }
                                Select {
                                    Layout.fillWidth: true
                                    model: [
                                        { value: "no", text: "Original" },
                                        { value: "16:9", text: "16:9" },
                                        { value: "4:3", text: "4:3" },
                                        { value: "1.85:1", text: "1.85:1" },
                                        { value: "2.35:1", text: "2.35:1" },
                                        { value: "2.39:1", text: "2.39:1" }
                                    ]
                                    textRole: "text"; valueRole: "value"
                                    onActivated: Player.setOption("video-aspect-override", currentValue)
                                }
                                ValueSlider {
                                    Layout.fillWidth: true
                                    label: "Pan & Scan (Balken wegschneiden)"
                                    from: 0; to: 1; stepSize: 0.05; decimals: 2
                                    value: picPane.panscan
                                    onMoved: v => { picPane.panscan = v; Player.setOption("panscan", v) }
                                }
                                ValueSlider {
                                    Layout.fillWidth: true
                                    label: "Zoom"
                                    from: -0.5; to: 0.5; stepSize: 0.01; decimals: 2
                                    value: picPane.zoom
                                    onMoved: v => { picPane.zoom = v; Player.setOption("video-zoom", v) }
                                }

                                SectionLabel { text: "Bildregler" }
                                Repeater {
                                    model: [
                                        { key: "brightness", label: "Helligkeit" },
                                        { key: "contrast", label: "Kontrast" },
                                        { key: "saturation", label: "Sättigung" },
                                        { key: "gamma", label: "Gamma" }
                                    ]
                                    delegate: ValueSlider {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        label: modelData.label
                                        from: -100; to: 100
                                        value: picPane.eq[modelData.key]
                                        onMoved: v => picPane.setEq(modelData.key, v)
                                    }
                                }
                                Toggle {
                                    Layout.fillWidth: true
                                    label: "Deinterlacing"
                                    hint: "Für 1080i-Material (Konzerte, Doku)"
                                    onToggled: Player.setOption("deinterlace", checked)
                                }

                                SectionLabel { text: "3D-Quelle" }
                                Select {
                                    Layout.fillWidth: true
                                    enabled: !Player.mvcActive
                                    model: [
                                        { value: "none", text: "2D" },
                                        { value: "sbsl", text: Player.mvcActive ? "Blu-ray 3D (MVC) – automatisch" : "Side-by-Side (Full)" },
                                        { value: "sbsr", text: "Blu-ray 3D (MVC, Basis rechts) – automatisch" },
                                        { value: "sbs2l", text: "Side-by-Side (Half)" },
                                        { value: "abl", text: "Top-and-Bottom (Full)" },
                                        { value: "ab2l", text: "Top-and-Bottom (Half)" }
                                    ]
                                    textRole: "text"; valueRole: "value"
                                    currentIndex: indexFor(Player.stereoInput)
                                    onActivated: Player.stereoInput = currentValue
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: Theme.textFaint
                                    font.pixelSize: 11
                                    text: (Player.mvcActive ? "Beide Ansichten der Blu-ray werden dekodiert. " : "")
                                          + "Ausgabe im Format des Profils: " + win.stereoLabel(Profiles.current.stereoOut)
                                          + (Profiles.current.stereoOut && Profiles.current.stereoOut !== "none" ? "." : " – 3D-Quellen werden als 2D (linkes Auge) gezeigt.")
                                }

                                Button {
                                    text: "Alles zurücksetzen"
                                    flat: true
                                    palette.windowText: Theme.textDim
                                    onClicked: {
                                        ["brightness", "contrast", "saturation", "gamma"].forEach(k => picPane.setEq(k, 0))
                                        picPane.zoom = 0; picPane.panscan = 0
                                        Player.setOption("video-zoom", 0)
                                        Player.setOption("panscan", 0)
                                        Player.setOption("video-aspect-override", "no")
                                    }
                                }
                            }
                        }

                        // ---- Ausgabe ----
                        ScrollView {
                            id: outPane
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                width: outPane.availableWidth
                                spacing: 10

                                SectionLabel { text: "Aktives Profil" }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: profCol.implicitHeight + 24
                                    radius: Theme.radius
                                    color: Theme.raised
                                    ColumnLayout {
                                        id: profCol
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 6
                                        Text { text: Profiles.current.name || ""; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Text { text: Profiles.current.description || ""; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: text.length > 0 }
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Chip { text: Profiles.current.fullscreen ? "Vollbild" : "Fenster" }
                                            Chip { text: Profiles.current.hdr === "passthrough" ? "HDR-Passthrough" : Profiles.current.hdr === "tonemap" ? "Tonemapping" + (Profiles.current.targetPeak ? " " + Profiles.current.targetPeak + " nits" : "") : "HDR auto"; tint: Theme.hdr }
                                            Chip { text: Profiles.current.matchRefreshRate ? "Bildrate-Anpassung" : "" }
                                            Chip { text: (Profiles.current.audioPassthrough || []).length ? "Bitstream" : ""; tint: Theme.good }
                                            Chip { text: Profiles.current.stereoOut && Profiles.current.stereoOut !== "none" ? "3D · " + win.stereoLabel(Profiles.current.stereoOut) : ""; tint: Theme.accent }
                                            Chip { text: Profiles.current.stereoOut && Profiles.current.stereoOut !== "none" && !Player.mvcCapable ? "Kein MVC-Decoder" : ""; tint: Theme.warn }
                                        }
                                        RowLayout {
                                            Button { text: "Bearbeiten"; flat: true; palette.windowText: Theme.accent; onClicked: editor.openFor(Profiles.current) }
                                            Button { text: "Duplizieren"; flat: true; palette.windowText: Theme.textDim; onClicked: Profiles.currentId = Profiles.duplicateProfile(Profiles.currentId) }
                                            Button { text: "Neu"; flat: true; palette.windowText: Theme.textDim; onClicked: editor.openFor(Profiles.defaults()) }
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: Player.outputStatus
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    SectionLabel { text: "Ausgabegeräte"; Layout.fillWidth: true }
                                    IconButton { iconName: "refresh"; size: 28; iconSize: 16; tip: "Neu einlesen"; onClicked: Displays.refresh() }
                                }
                                Repeater {
                                    model: Displays.outputs
                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property bool target: (Profiles.current.output || "") === modelData.id || (!Profiles.current.output && modelData.primary)
                                        Layout.fillWidth: true
                                        implicitHeight: 54
                                        radius: Theme.radiusSmall
                                        color: target ? Theme.accentSoft : Theme.raised
                                        border.color: target ? Theme.accent : "transparent"
                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 2
                                            Text { text: modelData.name + (modelData.primary ? "  (Haupt)" : ""); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text {
                                                text: modelData.width + "×" + modelData.height + " @ " + modelData.refresh + " Hz"
                                                      + (modelData.hdrSupported ? (modelData.hdrEnabled ? " · HDR an" : " · HDR-fähig") : "")
                                                color: Theme.textDim; font.pixelSize: 11; font.family: Theme.mono
                                            }
                                        }
                                    }
                                }

                                SectionLabel { text: "Effektive mpv-Optionen" }
                                TextArea {
                                    Layout.fillWidth: true
                                    readOnly: true
                                    wrapMode: TextEdit.NoWrap
                                    color: Theme.textDim
                                    font.family: Theme.mono
                                    font.pixelSize: 11
                                    text: {
                                        const o = Profiles.mpvOptions(Profiles.current)
                                        return Object.keys(o).sort().map(k => k + "=" + o[k]).join("\n")
                                    }
                                    background: Rectangle { color: Theme.raised; radius: Theme.radiusSmall }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============ Statuszeile ============
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Theme.panel
            Rectangle { width: parent.width; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 16
                Text {
                    Layout.fillWidth: true
                    text: Player.lastError || Player.outputStatus
                    color: Player.lastError ? Theme.bad : Theme.textFaint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text {
                    visible: !Player.idle && Player.cacheSeconds > 0
                    text: "Puffer " + Player.cacheSeconds.toFixed(0) + " s"
                    color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.mono
                }
                Text {
                    text: win.vinfo.displayFps > 0 ? "Anzeige " + win.vinfo.displayFps.toFixed(3) + " Hz" : ""
                    color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.mono
                }
                Text { text: "Lumen " + Qt.application.version; color: Theme.textFaint; font.pixelSize: 11 }
            }
        }
    }
}
