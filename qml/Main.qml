import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Lumen.Core

// Steuerfenster. Das Bild läuft im separaten Player-Fenster (mpv, nativ).
ApplicationWindow {
    id: win
    width: 1240
    height: 720
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: Player.idle || !nowTitle ? qsTr("Lumen") : nowTitle + " – " + qsTr("Lumen")
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
    // Name dessen, was gerade läuft (große Zeile links, Fenstertitel)
    readonly property string nowTitle: (Dcp.active && Dcp.current.title) ? Dcp.current.title
        : (isDvd && DvdNav.discTitle) ? DvdNav.discTitle
        : (Player.isDisc && Disc.info.discName) ? Disc.info.discName
        : (Player.isDisc && selectedDrive && selectedDrive.label) ? selectedDrive.label
        : Player.mediaTitle
    // Position, an der eine Datei aus „Zuletzt gespielt“ fortgesetzt wird
    property real pendingResume: 0
    readonly property var selectedDrive: driveSelect.currentIndex >= 0 ? Drives.drives[driveSelect.currentIndex] : null
    readonly property var vinfo: Player.videoInfo
    readonly property var ainfo: Player.audioInfo

    // Im Menümodus liefert libbluray bzw. libdvdnav Zeit, Kapitel und Spulen
    readonly property var nav: DvdNav.active ? DvdNav : Nav
    readonly property bool isDvd: DvdNav.active
    readonly property bool navMode: Nav.active || DvdNav.active
    readonly property real curPos: navMode ? nav.position : Player.position
    readonly property real curDur: navMode ? nav.duration : Player.duration
    // Audio-CD: Kapitel = Tracks, Namen aus CD-Text bzw. von einem Plugin (Disc.info.titles)
    readonly property var curChapters: navMode ? nav.chapters
        : (Player.sourceKind === "cdda" && Disc.info.titles)
            ? Player.chapters.map((c, i) => (Disc.info.titles[i] && Disc.info.titles[i].name)
                                            ? Object.assign({}, c, { title: Disc.info.titles[i].label }) : c)
            : Player.chapters
    readonly property int curChapter: navMode ? nav.chapter : Player.currentChapter
    function outputName(id) {
        const o = Displays.outputs.find(x => x.id === id)
        return o ? o.name + " (" + o.width + "×" + o.height + ")" : "–"
    }
    function seekAbs(s) { if (navMode) nav.seek(s); else Player.seek(s, false) }
    function seekRel(d) { if (navMode) nav.seekRelative(d); else Player.seek(d, true) }
    function nextChapter() { if (navMode) nav.nextChapter(); else Player.nextChapter() }
    function prevChapter() { if (navMode) nav.prevChapter(); else Player.prevChapter() }
    function setChapter(i) { if (navMode) nav.setChapter(i); else Player.setChapter(i) }

    function kindLabel(k) {
        return ({ bluray: qsTr("Blu-ray"), dvd: qsTr("DVD-Video"), hddvd: qsTr("HD DVD"), vcd: qsTr("Video-CD"), svcd: qsTr("Super Video-CD"),
                  cdda: qsTr("Audio-CD"), dcp: qsTr("Digital Cinema Package"), file: qsTr("Datei") })[k] || qsTr("Datei")
    }
    function localPath(url) {
        return decodeURIComponent(url.toString().replace(/^file:\/{2,3}/, Qt.platform.os === "windows" ? "" : "/"))
    }
    // Beliebige Quelle öffnen: Disc-Info lesen (außer DCP/Datei) und abspielen
    function openPath(path, withMenu) {
        currentDevice = path
        const kind = Player.detectKind(path)
        if (kind === "dcp") { Dcp.open(path, true); settings.tab = 5; return }
        if (kind !== "file") Disc.scan(path); else Disc.clear()
        Player.openSource(path, withMenu ? "menu" : "main", -1)
    }

    function showHelp() { helpDialog.open() }
    function openRecentIndex(i) { if (Recent.items[i]) openRecent(Recent.items[i]) }
    function openRecent(item) {
        pendingResume = item.kind === "file" && item.position > 5 ? item.position : 0
        openPath(item.path, settings.startWithMenu)
    }
    // Aus dem Dateimanager oder Browser ins Fenster gezogen
    function openDropped(drop) {
        if (drop.hasUrls && drop.urls.length > 0) {
            const u = drop.urls[0].toString()
            if (u.startsWith("file:")) openPath(localPath(u), settings.startWithMenu)
            else { currentDevice = ""; Disc.clear(); Player.openStream(u) }
            return true
        }
        const text = drop.hasText ? drop.text.trim() : ""
        if (/^[a-z][a-z0-9+.-]*:\/\//i.test(text)) { currentDevice = ""; Disc.clear(); Player.openStream(text); return true }
        return false
    }

    function stereoLabel(v) {
        return ({ none: "2D", fp: qsTr("HDMI Frame Packing"), sbs2l: qsTr("Side-by-Side Half"), sbsl: qsTr("Side-by-Side Full"),
                  ab2l: qsTr("Top-and-Bottom Half"), abl: qsTr("Top-and-Bottom Full"), irl: qsTr("Zeilenverschachtelt"),
                  arcd: qsTr("Anaglyph") })[v || "none"] || v
    }

    function playDrive(drive, withMenu) {
        if (!drive) return
        const menu = withMenu === undefined ? settings.startWithMenu : withMenu
        openPath(drive.path, menu)
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
            if ((Player.isDisc || Player.sourceKind === "dcp") && Player.device) win.currentDevice = Player.device
        }
    }

    Connections {
        target: Player
        // Dauer und "läuft" meldet mpv in beliebiger Reihenfolge
        function tryResume() {
            if (win.pendingResume > 0 && !Player.idle && Player.duration > win.pendingResume) {
                Player.seek(win.pendingResume, false)
                win.pendingResume = 0
            }
        }
        function onDurationChanged() { tryResume() }
        function onIdleChanged() { tryResume() }
    }

    Connections {
        target: Drives
        function onDiscInserted(drive) {
            if (settings.autoPlay && Player.idle) win.playDrive(drive)
            else if (drive.kind !== "dcp") Disc.scan(drive.path)
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Datei oder ISO öffnen")
        nameFilters: [qsTr("Medien (*.iso *.mkv *.m2ts *.mts *.ts *.mp4 *.mov *.webm *.bdmv *.vob *.ifo *.evo *.mpg *.dat *.cue *.bin *.nrg *.mxf *.xml)"),
                      qsTr("Disc-Abbilder (*.iso *.cue *.bin *.nrg)"), qsTr("Alle Dateien (*)")]
        onAccepted: win.openPath(win.localPath(selectedFile), settings.startWithMenu)
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Disc- oder DCP-Ordner öffnen (BDMV, VIDEO_TS, HVDVD_TS, MPEGAV, ASSETMAP)")
        onAccepted: win.openPath(win.localPath(selectedFolder), settings.startWithMenu)
    }

    FileDialog {
        id: subtitleDialog
        title: qsTr("Untertiteldatei laden")
        nameFilters: [qsTr("Untertitel (*.srt *.ass *.ssa *.sub *.idx *.sup *.vtt *.smi)"), qsTr("Alle Dateien (*)")]
        onAccepted: Player.addSubtitleFile(selectedFile)
    }

    ProfileEditor { id: editor }
    HelpDialog { id: helpDialog }

    // Dateien, Ordner und Links ins Fenster ziehen
    DropArea {
        id: dropArea
        anchors.fill: parent
        onDropped: drop => { if (win.openDropped(drop)) drop.acceptProposedAction() }
    }

    // ------------------------------------------------------------------
    // Tastatur (Steuerfenster)
    // ------------------------------------------------------------------
    Shortcut { sequence: "Space"; onActivated: Player.togglePause() }
    // Im Disc-Menü steuern Pfeile/Enter die Menüauswahl
    Shortcut { sequence: "Left"; onActivated: if (!(win.nav.menuVisible && win.nav.key("left"))) win.seekRel(-10) }
    Shortcut { sequence: "Right"; onActivated: if (!(win.nav.menuVisible && win.nav.key("right"))) win.seekRel(10) }
    Shortcut { sequence: "Shift+Left"; onActivated: win.seekRel(-60) }
    Shortcut { sequence: "Shift+Right"; onActivated: win.seekRel(60) }
    Shortcut { sequence: "Up"; onActivated: if (!(win.nav.menuVisible && win.nav.key("up"))) Player.setVolume(Math.min(Player.volumeMax, Player.volume + 5)) }
    Shortcut { sequence: "Down"; onActivated: if (!(win.nav.menuVisible && win.nav.key("down"))) Player.setVolume(Math.max(0, Player.volume - 5)) }
    Shortcut { sequence: "Return"; enabled: win.nav.menuVisible; onActivated: win.nav.key("enter") }
    Shortcut { sequence: "Home"; enabled: win.navMode; onActivated: win.nav.key("menu") }
    Shortcut { sequence: "End"; enabled: win.navMode; onActivated: win.nav.key("popup") }
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
    Shortcut { sequence: "Ctrl+D"; onActivated: settings.tab = 5 }
    Shortcut { sequence: "F1"; onActivated: helpDialog.open() }

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
                    Image {
                        source: "qrc:/qt/qml/Lumen/resources/logo/lumen-emblem-small.png"
                        height: 24; width: 50
                        fillMode: Image.PreserveAspectFit
                        smooth: true; mipmap: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text { text: qsTr("LUMEN"); color: Theme.text; font.pixelSize: 15; font.weight: Font.Bold; font.letterSpacing: 4 }
                }

                Select {
                    id: driveSelect
                    Layout.preferredWidth: 300
                    model: Drives.drives
                    textRole: "title"
                    popupWidth: 380
                    placeholder: Drives.scanning ? qsTr("Suche Laufwerke …") : qsTr("Kein Laufwerk gefunden")
                    onActivated: if (win.selectedDrive && win.selectedDrive.kind && win.selectedDrive.kind !== "dcp") Disc.scan(win.selectedDrive.path)
                }
                IconButton {
                    iconName: "disc"
                    tip: qsTr("Hauptfilm direkt abspielen")
                    enabled: !!win.selectedDrive && !!win.selectedDrive.hasDisc
                    onClicked: win.playDrive(win.selectedDrive, false)
                }
                IconButton {
                    iconName: "menu"
                    tip: qsTr("Mit Disc-Menü starten (Blu-ray, DVD)")
                    enabled: !!win.selectedDrive && !!win.selectedDrive.hasDisc
                             && (win.selectedDrive.kind === "bluray" ? Nav.available : win.selectedDrive.kind === "dvd")
                    onClicked: win.playDrive(win.selectedDrive, true)
                }
                IconButton { iconName: "eject"; tip: qsTr("Auswerfen (Strg+E)"); enabled: !!win.selectedDrive; onClicked: win.ejectSelected() }
                IconButton {
                    iconName: "folder"
                    tip: qsTr("Datei, Abbild, Disc- oder DCP-Ordner öffnen")
                    onClicked: openMenu.popup()
                    Menu {
                        id: openMenu
                        MenuItem { text: qsTr("Datei oder Abbild (ISO, CUE/BIN) …"); onTriggered: fileDialog.open() }
                        MenuItem { text: qsTr("Disc-Ordner (Blu-ray, DVD, HD DVD, VCD) …"); onTriggered: folderDialog.open() }
                        MenuItem { text: qsTr("DCP (Kino) …"); onTriggered: folderDialog.open() }
                        MenuItem { text: qsTr("Stream-Link oder Medienserver …"); onTriggered: settings.tab = 6 }
                        background: Rectangle { implicitWidth: 220; color: Theme.raised; border.color: Theme.line; radius: Theme.radiusSmall }
                    }
                }

                Item { Layout.fillWidth: true }

                Text { text: qsTr("Ausgabe"); color: Theme.textFaint; font.pixelSize: 12 }
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
                IconButton { iconName: "tune"; tip: qsTr("Profil bearbeiten"); onClicked: editor.openFor(Profiles.current) }
                IconButton { iconName: "help"; tip: qsTr("Hilfe und Tastenkürzel (F1)"); onClicked: helpDialog.open() }
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

                    Image {
                        // Cover der erkannten Audio-CD (von einem Plugin geliefert)
                        visible: status === Image.Ready
                        source: (Player.sourceKind === "cdda" && Disc.info.meta && Disc.info.meta.cover) || ""
                        Layout.preferredWidth: 132; Layout.preferredHeight: 132
                        Layout.bottomMargin: 6
                        sourceSize.width: 264; sourceSize.height: 264
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    SectionLabel { text: win.kindLabel(Player.sourceKind) + (Dcp.active && Dcp.current.contentKind ? " · " + Dcp.current.contentKind : "") }
                    Text {
                        Layout.fillWidth: true
                        text: win.nowTitle
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
                                parts.push(!win.nav.menuMode ? qsTr("Titel") : win.nav.menuVisible ? qsTr("Disc-Menü") : qsTr("Menümodus"))
                                if (win.nav.playlist >= 0) parts.push(("0000" + win.nav.playlist).slice(-5) + ".mpls")
                                if (win.isDvd && DvdNav.title > 0) parts.push(qsTr("Titel ") + DvdNav.title + " / " + DvdNav.titles)
                            } else if (Player.isDisc && Player.currentTitle >= 0) {
                                parts.push(qsTr("Titel ") + (Player.currentTitle + 1))
                            }
                            if (win.curChapters.length) parts.push(qsTr("Kapitel ") + (win.curChapter + 1) + " / " + win.curChapters.length)
                            if (Player.sourceKind === "cdda") {
                                const track = (Disc.info.titles || [])[win.curChapter]
                                if (track && track.name) parts.push(track.name + (track.artist ? " – " + track.artist : ""))
                            }
                            if (Player.sourceKind === "file") parts.push(Player.path)
                            else if (Dcp.active) parts.push(Dcp.current.standard + " · " + Dcp.current.reels + qsTr(" Rolle(n)"))
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
                        Chip { text: win.vinfo.fps > 0 ? win.vinfo.fps.toFixed(3) + qsTr(" fps") : "" }
                        Chip { text: (win.ainfo.codec || "") + (win.ainfo.channels ? " " + win.ainfo.channels : "") }
                        Chip { text: win.ainfo.passthrough ? qsTr("BITSTREAM") : ""; tint: Theme.good }
                        Chip {
                            text: Player.mvcActive ? qsTr("3D MVC → ") + win.stereoLabel(Profiles.current.stereoOut)
                                : Player.stereoInput !== "none" ? "3D"
                                : Disc.info.has3d ? qsTr("3D-Disc (2D)") : ""
                            tint: Theme.accent
                            filled: Player.mvcActive
                        }
                        Chip { text: Player.buffering ? qsTr("Puffert …") : ""; tint: Theme.warn }
                        Chip { text: Player.speed !== 1 ? Player.speed.toFixed(2) + "×" : ""; tint: Theme.accent }
                        Chip { text: Player.embedded ? qsTr("Eingebettet · SDR") : ""; tint: Theme.textDim }
                        Chip { text: Dcp.active && Dcp.current.encrypted ? qsTr("Entschlüsselt (KDM)") : ""; tint: Theme.good }
                        Chip { text: Dcp.active && Dcp.reduction > 0 ? qsTr("J2K 1/") + Math.pow(2, Dcp.reduction) : ""; tint: Theme.warn }
                        Chip { text: Dcp.active && Dcp.fader !== 7 ? qsTr("Fader ") + Dcp.fader.toFixed(1) : ""; tint: Theme.textDim }
                        Chip { text: Player.droppedFrames > 0 && !Player.idle ? Player.droppedFrames + (Player.droppedFrames === 1 ? qsTr(" Bild verworfen") : qsTr(" Bilder verworfen")) : ""; tint: Theme.warn }
                        Chip { text: win.isDvd && DvdNav.angles > 1 ? qsTr("Winkel ") + DvdNav.angle + "/" + DvdNav.angles : ""; tint: Theme.accent }
                    }

                    // Video-CD-Wiedergabesteuerung (PBC): Auswahlnummern und Sprungtasten
                    ColumnLayout {
                        visible: VcdNav.active
                        Layout.topMargin: 10
                        spacing: 6
                        Text {
                            text: VcdNav.selection ? qsTr("VCD-Menü – Auswahl per Zifferntaste") : qsTr("VCD-Wiedergabesteuerung")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                        Flow {
                            Layout.fillWidth: true
                            visible: VcdNav.selection
                            spacing: 4
                            Repeater {
                                model: VcdNav.state.choices || []
                                delegate: Button {
                                    required property var modelData
                                    width: 36; height: 36
                                    text: modelData
                                    focusPolicy: Qt.NoFocus
                                    font.pixelSize: 12; font.weight: Font.DemiBold
                                    onClicked: String(modelData).split("").forEach(c => VcdNav.key(c))
                                    contentItem: Text { text: parent.text; color: Theme.bg; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    background: Rectangle { radius: 18; color: parent.down ? "#c9ccd6" : Theme.text }
                                }
                            }
                        }
                        RowLayout {
                            spacing: 2
                            Button { text: qsTr("Zurück"); flat: true; enabled: !!VcdNav.state.prev; palette.windowText: Theme.text; opacity: enabled ? 1 : 0.35; onClicked: VcdNav.key("prev") }
                            Button { text: qsTr("OK"); flat: true; enabled: !!VcdNav.state["default"]; palette.windowText: Theme.accent; opacity: enabled ? 1 : 0.35; onClicked: VcdNav.key("enter") }
                            Button { text: qsTr("Weiter"); flat: true; enabled: !!VcdNav.state.next; palette.windowText: Theme.text; opacity: enabled ? 1 : 0.35; onClicked: VcdNav.key("next") }
                            Button { text: qsTr("Menü"); flat: true; enabled: !!VcdNav.state["return"]; palette.windowText: Theme.text; opacity: enabled ? 1 : 0.35; onClicked: VcdNav.key("return") }
                        }
                    }

                    // Fernbedienung für Disc-Menüs
                    RowLayout {
                        visible: win.navMode && win.nav.menuMode
                        Layout.topMargin: 10
                        spacing: 18

                        Grid {
                            columns: 3
                            spacing: 4
                            Item { width: 36; height: 36 }
                            IconButton { iconName: "up"; tip: qsTr("Hoch"); onClicked: win.nav.key("up") }
                            Item { width: 36; height: 36 }
                            IconButton { iconName: "left"; tip: qsTr("Links"); onClicked: win.nav.key("left") }
                            Button {
                                width: 36; height: 36
                                text: qsTr("OK")
                                focusPolicy: Qt.NoFocus
                                font.pixelSize: 11; font.weight: Font.Bold
                                onClicked: win.nav.key("enter")
                                contentItem: Text { text: parent.text; color: Theme.bg; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 18; color: parent.down ? "#c9ccd6" : Theme.text }
                            }
                            IconButton { iconName: "right"; tip: qsTr("Rechts"); onClicked: win.nav.key("right") }
                            Item { width: 36; height: 36 }
                            IconButton { iconName: "down"; tip: qsTr("Runter"); onClicked: win.nav.key("down") }
                            Item { width: 36; height: 36 }
                        }
                        ColumnLayout {
                            spacing: 6
                            Button {
                                text: qsTr("Hauptmenü")
                                flat: true
                                palette.windowText: Theme.text
                                onClicked: win.nav.key("menu")
                            }
                            Button {
                                text: win.isDvd ? qsTr("Titelmenü") : qsTr("Pop-up-Menü")
                                flat: true
                                enabled: win.nav.popupAvailable
                                palette.windowText: Theme.text
                                onClicked: win.nav.key("popup")
                            }
                            RowLayout {
                                visible: win.isDvd
                                spacing: 2
                                Button { text: qsTr("Ton"); flat: true; palette.windowText: Theme.textDim; onClicked: DvdNav.key("audio") }
                                Button { text: qsTr("Untertitel"); flat: true; palette.windowText: Theme.textDim; onClicked: DvdNav.key("subtitle") }
                                Button { text: qsTr("Zurück"); flat: true; palette.windowText: Theme.textDim; onClicked: DvdNav.key("back") }
                                Button {
                                    text: qsTr("Winkel ") + DvdNav.angle
                                    visible: DvdNav.angles > 1
                                    flat: true; palette.windowText: Theme.accent
                                    onClicked: DvdNav.setAngle(DvdNav.angle % DvdNav.angles + 1)
                                }
                            }
                            Text {
                                text: qsTr("Pfeile/Enter/Maus funktionieren auch im Player-Fenster · Pos1 = Hauptmenü · Ende = ") + (win.isDvd ? qsTr("Titelmenü") : qsTr("Pop-up"))
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
                    SectionLabel { text: qsTr("Bereit") }
                    Text {
                        text: Drives.drives.some(d => d.kind === "dcp") && !Drives.drives.some(d => d.kind && d.kind !== "dcp") ? qsTr("DCP gefunden – bereit zur Vorführung")
                            : Drives.drives.some(d => !!d.kind) ? qsTr("Disc erkannt – bereit zur Wiedergabe") : qsTr("Disc einlegen oder Datei öffnen")
                        color: Theme.text
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                    }
                    // Sprache direkt auf der Startseite wählen
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: I18n.languages
                            delegate: AbstractButton {
                                id: langBtn
                                required property var modelData
                                readonly property bool current: I18n.effective === modelData.code
                                focusPolicy: Qt.NoFocus
                                implicitWidth: langText.implicitWidth + 18
                                implicitHeight: 26
                                onClicked: I18n.language = modelData.code
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                contentItem: Text {
                                    id: langText
                                    text: langBtn.modelData.name
                                    color: langBtn.current ? Theme.bg : langBtn.hovered ? Theme.text : Theme.textDim
                                    font.pixelSize: 12
                                    font.weight: langBtn.current ? Font.DemiBold : Font.Normal
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 13
                                    color: langBtn.current ? Theme.text : "transparent"
                                    border.color: langBtn.current ? "transparent" : Theme.line
                                }
                            }
                        }
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
                                    Image { source: Theme.icon(modelData.kind === "dcp" ? "folder" : "disc"); sourceSize: Qt.size(28, 28); opacity: 0.8 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { Layout.fillWidth: true; text: modelData.label || modelData.device; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                        Text {
                                            Layout.fillWidth: true
                                            text: (modelData.kindLabel || qsTr("Disc")) + (modelData.is3d ? qsTr(" · 3D") : "") + (modelData.hardware ? " · " + modelData.hardware : "")
                                            color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        visible: Recent.items.length > 0
                        Layout.fillWidth: true
                        SectionLabel { text: qsTr("Zuletzt gespielt"); Layout.fillWidth: true }
                        Button {
                            text: qsTr("Liste leeren")
                            flat: true
                            focusPolicy: Qt.NoFocus
                            font.pixelSize: 11
                            palette.windowText: Theme.textFaint
                            onClicked: Recent.clear()
                        }
                    }
                    Flow {
                        id: recentFlow
                        visible: Recent.items.length > 0
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: Recent.items.slice(0, 4)
                            delegate: Rectangle {
                                id: recentTile
                                required property var modelData
                                width: Math.max(220, Math.min(360, (recentFlow.width - 10) / 2))
                                height: 60
                                radius: Theme.radius
                                color: recentHover.hovered ? Theme.hover : Theme.raised
                                border.color: Theme.line
                                clip: true
                                HoverHandler { id: recentHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: if (!recentRemove.hovered) win.openRecent(recentTile.modelData) }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 8
                                    spacing: 12
                                    Image {
                                        source: Theme.icon(recentTile.modelData.kind === "file" ? "play" : recentTile.modelData.kind === "dcp" ? "folder" : "disc")
                                        sourceSize: Qt.size(22, 22)
                                        opacity: 0.7
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: recentTile.modelData.title || recentTile.modelData.path
                                            color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: win.kindLabel(recentTile.modelData.kind)
                                                  + (recentTile.modelData.position > 5 ? "  ·  " + qsTr("Weiter bei %1").arg(Theme.time(recentTile.modelData.position)) : "")
                                            color: Theme.textDim; font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                    IconButton {
                                        id: recentRemove
                                        iconName: "close"
                                        size: 26; iconSize: 14
                                        opacity: recentHover.hovered ? 1 : 0
                                        tip: qsTr("Aus der Liste entfernen")
                                        onClicked: Recent.remove(recentTile.modelData.path)
                                    }
                                }
                                Rectangle {
                                    // Fortschritt der zuletzt erreichten Position
                                    visible: recentTile.modelData.position > 5 && recentTile.modelData.duration > 0
                                    anchors.bottom: parent.bottom
                                    height: 2
                                    width: parent.width * Math.min(1, recentTile.modelData.position / Math.max(1, recentTile.modelData.duration))
                                    color: Theme.accent
                                }
                            }
                        }
                    }
                    Toggle {
                        label: qsTr("Discs beim Einlegen automatisch abspielen")
                        checked: settings.autoPlay
                        onToggled: settings.autoPlay = checked
                        implicitWidth: 360
                    }
                    Toggle {
                        label: qsTr("Mit Disc-Menü starten")
                        hint: qsTr("Aus: Hauptfilm direkt abspielen")
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
                                IconButton { iconName: "frame-back"; tip: qsTr("Einzelbild zurück (,)"); onClicked: Player.frameBackStep() }
                                IconButton { iconName: "frame-fwd"; tip: qsTr("Einzelbild vor (.)"); onClicked: Player.frameStep() }
                                IconButton {
                                    iconName: "loop"
                                    active: Player.loopA >= 0
                                    tip: Player.loopB >= 0 ? qsTr("A-B-Schleife aufheben (L)") : Player.loopA >= 0 ? qsTr("Punkt B setzen (L)") : qsTr("Punkt A setzen (L)")
                                    onClicked: Player.cycleAbLoop()
                                }
                                IconButton { iconName: "camera"; tip: qsTr("Screenshot (S)"); onClicked: Player.screenshot() }
                            }

                            Item { Layout.fillWidth: true }

                            // Mitte: Transport
                            IconButton { iconName: "prev"; tip: qsTr("Vorheriges Kapitel (Bild ab)"); onClicked: win.prevChapter() }
                            IconButton { iconName: "rewind"; tip: qsTr("−10 s (←)"); onClicked: win.seekRel(-10) }
                            IconButton {
                                iconName: Player.paused || Player.idle ? "play" : "pause"
                                primary: true
                                size: 54
                                iconSize: 26
                                Layout.leftMargin: 8
                                Layout.rightMargin: 8
                                tip: qsTr("Wiedergabe / Pause (Leertaste)")
                                onClicked: {
                                    if (Player.idle && win.selectedDrive && win.selectedDrive.hasDisc) win.playDrive(win.selectedDrive)
                                    else Player.togglePause()
                                }
                            }
                            IconButton { iconName: "forward"; tip: qsTr("+10 s (→)"); onClicked: win.seekRel(10) }
                            IconButton { iconName: "next"; tip: qsTr("Nächstes Kapitel (Bild auf)"); onClicked: win.nextChapter() }
                            IconButton { iconName: "stop"; tip: qsTr("Stopp"); enabled: !Player.idle; onClicked: Player.stop() }

                            Item { Layout.fillWidth: true }

                            // rechts: Lautstärke + Fenster
                            RowLayout {
                                spacing: 2
                                Layout.preferredWidth: 220
                                IconButton { iconName: Player.muted || Player.volume === 0 ? "mute" : "volume"; tip: qsTr("Stumm (M)"); onClicked: Player.setMuted(!Player.muted) }
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
                                IconButton { iconName: "fullscreen"; active: Player.fullscreen; tip: qsTr("Vollbild im Player-Fenster (F)"); onClicked: Player.toggleFullscreen() }
                                IconButton { iconName: "info"; tip: qsTr("Statistik im Bild (I)"); onClicked: Player.toggleStats() }
                            }
                        }
                    }
                }
            }

            // ---------- Rechte Seite: Panels ----------
            Rectangle {
                Layout.preferredWidth: 450
                Layout.fillHeight: true
                color: Theme.panel
                Rectangle { width: 1; height: parent.height; color: Theme.line }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 1
                    spacing: 0

                    // Tabs
                    Flow {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Layout.topMargin: 10
                        spacing: 2
                        Repeater {
                            model: [qsTr("Titel"), qsTr("Kapitel"), qsTr("Ton"), qsTr("Untertitel"), qsTr("Bild"), qsTr("Kino"), qsTr("Streaming"), qsTr("Ausgabe"), qsTr("Plugins")]
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
                                visible: Disc.available && (Disc.busy || Disc.info.device !== undefined) && (Disc.info.kind || "bluray") !== "dcp"
                                implicitHeight: discStatus.implicitHeight + 20
                                radius: Theme.radiusSmall
                                color: Theme.raised
                                ColumnLayout {
                                    id: discStatus
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 4
                                    readonly property var i: Disc.info
                                    // Von CD-Text oder einem Plugin erkannte Disc: Cover, Name, Jahr, Quelle
                                    RowLayout {
                                        visible: !Disc.busy && !!discStatus.i.meta && !!discStatus.i.discName
                                        Layout.fillWidth: true
                                        Layout.bottomMargin: 4
                                        spacing: 10
                                        Image {
                                            visible: status === Image.Ready
                                            source: (discStatus.i.meta && discStatus.i.meta.cover) || ""
                                            Layout.preferredWidth: 56; Layout.preferredHeight: 56
                                            sourceSize.width: 112; sourceSize.height: 112
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                Layout.fillWidth: true
                                                text: discStatus.i.discName || ""
                                                color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: discStatus.i.meta ? [discStatus.i.meta.year, discStatus.i.meta.source].filter(s => s).join("  ·  ") : ""
                                                color: Theme.textDim; font.pixelSize: 12
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    Text {
                                        visible: Disc.busy
                                        text: qsTr("Lese Disc-Struktur …")
                                        color: Theme.textDim; font.pixelSize: 12
                                    }
                                    Text {
                                        visible: !Disc.busy && !!discStatus.i.error
                                        text: discStatus.i.error || ""
                                        color: Theme.bad; font.pixelSize: 12
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    }
                                    Flow {
                                        visible: !Disc.busy && !discStatus.i.error && (discStatus.i.kind || "bluray") !== "bluray"
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Chip { text: win.kindLabel(discStatus.i.kind || "") }
                                        Chip { text: discStatus.i.regions ? qsTr("Region ") + discStatus.i.regions : "" }
                                        Chip { text: (discStatus.i.titles || []).length + qsTr(" Titel") }
                                        Chip { text: discStatus.i.aacs ? qsTr("AACS – nicht entschlüsselt") : ""; tint: Theme.bad }
                                    }
                                    Flow {
                                        visible: !Disc.busy && !discStatus.i.error && (discStatus.i.kind || "bluray") === "bluray"
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Chip {
                                            text: !discStatus.i.aacsDetected ? qsTr("Kein AACS")
                                                : discStatus.i.aacsHandled ? qsTr("AACS · extern gelöst")
                                                : !discStatus.i.aacsLibrary ? qsTr("AACS · keine externe Bibliothek")
                                                : qsTr("AACS · Fehler ") + discStatus.i.aacsError
                                            tint: !discStatus.i.aacsDetected || discStatus.i.aacsHandled ? Theme.good : Theme.bad
                                        }
                                        Chip {
                                            text: discStatus.i.bdplusDetected ? (discStatus.i.bdplusHandled ? qsTr("BD+ · extern gelöst") : qsTr("BD+ · nicht gelöst")) : ""
                                            tint: discStatus.i.bdplusHandled ? Theme.good : Theme.warn
                                        }
                                        Chip { text: discStatus.i.bdjDetected ? qsTr("BD-J-Menü") : ""; tint: Theme.textDim }
                                        Chip { text: discStatus.i.has3d ? qsTr("3D-Inhalt") : ""; tint: Theme.accent }
                                    }
                                    Text {
                                        visible: !Disc.busy && !!discStatus.i.aacsDetected && !discStatus.i.aacsHandled
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: Theme.textFaint; font.pixelSize: 11
                                        text: qsTr("Lumen entschlüsselt nicht selbst. Die Disc muss über ein LibreDrive-Laufwerk mit einer vom System bereitgestellten AACS-Bibliothek lesbar sein.")
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                SectionLabel { text: Disc.info.kind === "cdda" ? qsTr("Tracks") : qsTr("Titel / Playlists"); Layout.fillWidth: true }
                                Button {
                                    text: qsTr("Disc-Menü")
                                    flat: true
                                    visible: win.currentDevice.length > 0 && Player.sourceKind !== "dcp"
                                             && (Disc.info.kind === "dvd" || (Disc.info.kind === "bluray" && Nav.available))
                                    palette.windowText: Theme.accent
                                    onClicked: Player.openSource(win.currentDevice, "menu", -1)
                                }
                                Button {
                                    text: qsTr("Hauptfilm")
                                    flat: true
                                    visible: win.currentDevice.length > 0 && Player.sourceKind !== "dcp" && !!Disc.info.kind
                                             && Disc.info.kind !== "dcp" && Disc.info.kind !== "file" && Disc.info.kind !== "cdda"
                                    palette.windowText: Theme.accent
                                    onClicked: Player.openSource(win.currentDevice, "main", -1)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: win.nav.status.length > 0
                                text: win.nav.status
                                wrapMode: Text.WordWrap
                                color: win.nav.active ? Theme.good : Theme.warn
                                font.pixelSize: 12
                            }
                            SelectList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                emptyText: Player.sourceKind === "dcp" ? qsTr("Digital Cinema Package: Kompositionen, Schlüssel und Programm im Tab „Kino“.")
                                         : Nav.available ? qsTr("Keine Disc geladen.\nStart mit Disc-Menü oder Titel hier direkt wählen.")
                                                         : qsTr("Keine Disc geladen.\nOhne libbluray gebaut – Titel werden direkt gewählt.")
                                model: {
                                    const t = Disc.info.titles
                                    const kind = Disc.info.kind || "bluray"
                                    if (t && t.length && kind !== "bluray")
                                        return t.map(x => ({
                                            label: (x.label || (qsTr("Titel ") + (x.title || x.index + 1))) + (x.main ? qsTr("   ★ Hauptfilm") : ""),
                                            detail: [x.artist || "", x.chapters && kind !== "cdda" ? x.chapters + qsTr(" Kap.") : ""].filter(s => s).join("  ·  "),
                                            trailing: x.duration > 0 ? Theme.time(x.duration) : "",
                                            selected: win.isDvd ? DvdNav.title === x.title : false,
                                            titleIndex: x.index,
                                            dvdTitle: kind === "dvd" ? x.title : -1
                                        }))
                                    if (Disc.available && t && t.length)
                                        return t.map(x => ({
                                            label: qsTr("Titel ") + (x.index + 1) + (x.main ? qsTr("   ★ Hauptfilm") : ""),
                                            detail: [x.video, x.audio, x.chapters + qsTr(" Kap."), ("0000" + x.playlist).slice(-5) + ".mpls"].filter(s => s).join("  ·  "),
                                            trailing: Theme.time(x.duration),
                                            selected: (win.navMode && Nav.playlist === x.playlist) || Player.path === "bd://mpls/" + x.playlist || (Player.path === "bd://longest" && x.main),
                                            playlist: x.playlist
                                        }))
                                    return Player.titles.map(x => ({
                                        label: qsTr("Titel ") + (x.index + 1),
                                        trailing: x.duration > 0 ? Theme.time(x.duration) : "",
                                        selected: x.index === Player.currentTitle,
                                        index: x.index
                                    }))
                                }
                                onPicked: (i, item) => {
                                    if (item.dvdTitle > 0 && DvdNav.active) DvdNav.playTitle(item.dvdTitle)
                                    else if (item.titleIndex !== undefined) Player.openSource(win.currentDevice, "title", item.titleIndex)
                                    else if (item.playlist !== undefined) Player.openPlaylist(win.currentDevice, item.playlist)
                                    else Player.setTitle(item.index)
                                }
                            }
                        }

                        // ---- Kapitel ----
                        SelectList {
                            emptyText: qsTr("Keine Kapitel")
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
                            SectionLabel { text: qsTr("Tonspur") }
                            SelectList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                emptyText: qsTr("Keine Tonspuren")
                                model: Player.audioTracks.map(t => ({ label: t.label, selected: t.id === Player.audioId, id: t.id }))
                                onPicked: (i, item) => Player.setAudioId(item.id)
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: !!win.ainfo.format
                                text: win.ainfo.passthrough ? qsTr("Bitstream an Receiver: ") + win.ainfo.format.replace("spdif-", "").toUpperCase()
                                                            : qsTr("PCM ") + (win.ainfo.channels || "") + (win.ainfo.samplerate ? " · " + (win.ainfo.samplerate / 1000) + qsTr(" kHz") : "")
                                color: win.ainfo.passthrough ? Theme.good : Theme.textDim
                                font.pixelSize: 12
                            }
                            Toggle {
                                Layout.fillWidth: true
                                label: qsTr("Nachtmodus")
                                hint: qsTr("Leise Stellen lauter, laute leiser – wirkt nicht bei Bitstream-Ausgabe")
                                checked: Player.nightMode
                                onToggled: Player.nightMode = checked
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Audio-Verzögerung")
                                from: -2; to: 2; stepSize: 0.01; decimals: 2; unit: qsTr(" s")
                                value: Player.audioDelay
                                onMoved: v => Player.setAudioDelay(v)
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Geschwindigkeit")
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
                            SectionLabel { text: qsTr("Untertitel") }
                            SelectList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: win.isDvd
                                       ? [{ label: qsTr("Aus (nur erzwungene)"), selected: DvdNav.subtitleStream < 0, id: -1 }]
                                         .concat(DvdNav.subtitleStreams.map(t => ({ label: t.label, selected: t.id === DvdNav.subtitleStream, id: t.id })))
                                       : [{ label: qsTr("Aus"), selected: Player.subtitleId === 0, id: 0 }]
                                         .concat(Player.subtitleTracks.map(t => ({ label: t.label, selected: t.id === Player.subtitleId, id: t.id })))
                                onPicked: (i, item) => win.isDvd ? DvdNav.selectSubtitle(item.id) : Player.setSubtitleId(item.id)
                            }
                            Button {
                                text: qsTr("Untertiteldatei laden …")
                                flat: true
                                focusPolicy: Qt.NoFocus
                                enabled: !Player.idle && !win.navMode
                                opacity: enabled ? 1 : 0.4
                                palette.windowText: Theme.accent
                                onClicked: subtitleDialog.open()
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                visible: Nav.mvcActive
                                label: qsTr("3D-Tiefe (Untertitel & Menü)")
                                from: -40; to: 40; stepSize: 1; unit: qsTr(" px")
                                value: Nav.subtitleDepth
                                onMoved: v => Nav.subtitleDepth = Math.round(v)
                            }
                            Toggle {
                                Layout.fillWidth: true
                                visible: !Nav.mvcActive && !win.isDvd
                                label: qsTr("Nur erzwungene Untertitel")
                                hint: qsTr("Zeigt nur als 'forced' markierte Einblendungen (PGS)")
                                onToggled: Player.setOption("sub-forced-events-only", checked)
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Verzögerung")
                                from: -5; to: 5; stepSize: 0.05; decimals: 2; unit: qsTr(" s")
                                value: Player.subDelay
                                onMoved: v => Player.setSubDelay(v)
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Größe")
                                from: 0.5; to: 2; stepSize: 0.05; decimals: 2; unit: "×"; defaultValue: 1
                                value: subsPane.subScale
                                onMoved: v => { subsPane.subScale = v; Player.setOption("sub-scale", v) }
                            }
                            ValueSlider {
                                Layout.fillWidth: true
                                label: qsTr("Position (für 2.39:1-Leinwände nach oben ziehen)")
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

                                SectionLabel { text: qsTr("Seitenverhältnis & Ausschnitt") }
                                Select {
                                    Layout.fillWidth: true
                                    model: [
                                        { value: "no", text: qsTr("Original") },
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
                                    label: qsTr("Pan & Scan (Balken wegschneiden)")
                                    from: 0; to: 1; stepSize: 0.05; decimals: 2
                                    value: picPane.panscan
                                    onMoved: v => { picPane.panscan = v; Player.setOption("panscan", v) }
                                }
                                ValueSlider {
                                    Layout.fillWidth: true
                                    label: qsTr("Zoom")
                                    from: -0.5; to: 0.5; stepSize: 0.01; decimals: 2
                                    value: picPane.zoom
                                    onMoved: v => { picPane.zoom = v; Player.setOption("video-zoom", v) }
                                }

                                SectionLabel { text: qsTr("Bildregler") }
                                Repeater {
                                    model: [
                                        { key: "brightness", label: qsTr("Helligkeit") },
                                        { key: "contrast", label: qsTr("Kontrast") },
                                        { key: "saturation", label: qsTr("Sättigung") },
                                        { key: "gamma", label: qsTr("Gamma") }
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
                                    label: qsTr("Deinterlacing")
                                    hint: qsTr("Für 1080i-Material (Konzerte, Doku)")
                                    onToggled: Player.setOption("deinterlace", checked)
                                }

                                SectionLabel { text: qsTr("3D-Quelle") }
                                Select {
                                    Layout.fillWidth: true
                                    enabled: !Player.mvcActive
                                    model: [
                                        { value: "none", text: "2D" },
                                        { value: "sbsl", text: Player.mvcActive ? qsTr("Blu-ray 3D (MVC) – automatisch") : qsTr("Side-by-Side (Full)") },
                                        { value: "sbsr", text: qsTr("Blu-ray 3D (MVC, Basis rechts) – automatisch") },
                                        { value: "sbs2l", text: qsTr("Side-by-Side (Half)") },
                                        { value: "abl", text: qsTr("Top-and-Bottom (Full)") },
                                        { value: "ab2l", text: qsTr("Top-and-Bottom (Half)") }
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
                                    text: (Player.mvcActive ? qsTr("Beide Ansichten der Blu-ray werden dekodiert. ") : "")
                                          + qsTr("Ausgabe im Format des Profils: ") + win.stereoLabel(Profiles.current.stereoOut)
                                          + (Profiles.current.stereoOut && Profiles.current.stereoOut !== "none" ? "." : qsTr(" – 3D-Quellen werden als 2D (linkes Auge) gezeigt."))
                                }

                                Button {
                                    text: qsTr("Alles zurücksetzen")
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

                        // ---- Kino (DCP, KDM, Programm) ----
                        CinemaPane {}

                        // ---- Streaming (Links, Medienserver) ----
                        StreamingPane {}

                        // ---- Ausgabe ----
                        ScrollView {
                            id: outPane
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                width: outPane.availableWidth
                                spacing: 10

                                SectionLabel { text: qsTr("Sprache der Oberfläche") }
                                Select {
                                    Layout.fillWidth: true
                                    model: [{ code: "", name: qsTr("Systemsprache") }].concat(I18n.languages)
                                    textRole: "name"
                                    valueRole: "code"
                                    currentIndex: indexFor(I18n.language)
                                    onActivated: I18n.language = currentValue
                                }

                                SectionLabel { text: qsTr("Updates") }
                                Toggle {
                                    Layout.fillWidth: true
                                    label: qsTr("Beim Start nach Updates suchen")
                                    hint: Updater.status
                                    checked: Updater.autoCheck
                                    onToggled: Updater.autoCheck = checked
                                }
                                RowLayout {
                                    Button { text: qsTr("Jetzt prüfen"); flat: true; enabled: !Updater.busy; palette.windowText: Theme.accent; onClicked: Updater.check() }
                                    Button {
                                        visible: Updater.available
                                        text: Updater.canInstall ? qsTr("Lumen %1 installieren").arg(Updater.latestVersion) : qsTr("Download-Seite öffnen")
                                        flat: true; enabled: !Updater.busy; palette.windowText: Theme.accent
                                        onClicked: Updater.canInstall ? Updater.install() : Updater.openPage()
                                    }
                                }

                                SectionLabel { text: qsTr("Bildschirme") }
                                Toggle {
                                    Layout.fillWidth: true
                                    label: qsTr("Bildschirme automatisch zuordnen")
                                    hint: Displays.outputs.length > 1
                                          ? qsTr("Wiedergabe: %1 · Steuerung: %2").arg(win.outputName(Displays.mainOutput)).arg(win.outputName(Displays.controlOutput))
                                          : qsTr("Nur ein Bildschirm angeschlossen")
                                    checked: Displays.autoScreens
                                    onToggled: Displays.autoScreens = checked
                                }

                                SectionLabel { text: qsTr("Aktives Profil") }
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
                                            Chip { text: Profiles.current.fullscreen ? qsTr("Vollbild") : qsTr("Fenster") }
                                            Chip { text: Profiles.current.hdr === "passthrough" ? qsTr("HDR-Passthrough") : Profiles.current.hdr === "tonemap" ? qsTr("Tonemapping") + (Profiles.current.targetPeak ? " " + Profiles.current.targetPeak + qsTr(" nits") : "") : qsTr("HDR auto"); tint: Theme.hdr }
                                            Chip { text: Profiles.current.matchRefreshRate ? qsTr("Bildrate-Anpassung") : "" }
                                            Chip { text: (Profiles.current.audioPassthrough || []).length ? qsTr("Bitstream") : ""; tint: Theme.good }
                                            Chip { text: Profiles.current.stereoOut && Profiles.current.stereoOut !== "none" ? qsTr("3D · ") + win.stereoLabel(Profiles.current.stereoOut) : ""; tint: Theme.accent }
                                            Chip { text: Profiles.current.stereoOut && Profiles.current.stereoOut !== "none" && !Player.mvcCapable ? qsTr("Kein MVC-Decoder") : ""; tint: Theme.warn }
                                        }
                                        RowLayout {
                                            Button { text: qsTr("Bearbeiten"); flat: true; palette.windowText: Theme.accent; onClicked: editor.openFor(Profiles.current) }
                                            Button { text: qsTr("Duplizieren"); flat: true; palette.windowText: Theme.textDim; onClicked: Profiles.currentId = Profiles.duplicateProfile(Profiles.currentId) }
                                            Button { text: qsTr("Neu"); flat: true; palette.windowText: Theme.textDim; onClicked: editor.openFor(Profiles.defaults()) }
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
                                    SectionLabel { text: qsTr("Ausgabegeräte"); Layout.fillWidth: true }
                                    IconButton { iconName: "refresh"; size: 28; iconSize: 16; tip: qsTr("Neu einlesen"); onClicked: Displays.refresh() }
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
                                            Text { text: modelData.name + (modelData.primary ? qsTr("  (Haupt)") : ""); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text {
                                                text: modelData.width + "×" + modelData.height + " @ " + modelData.refresh + qsTr(" Hz")
                                                      + (modelData.hdrSupported ? (modelData.hdrEnabled ? qsTr(" · HDR an") : qsTr(" · HDR-fähig")) : "")
                                                color: Theme.textDim; font.pixelSize: 11; font.family: Theme.mono
                                            }
                                        }
                                    }
                                }

                                SectionLabel { text: qsTr("Effektive mpv-Optionen") }
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

                        // ---- Plugins ----
                        PluginsPane {}
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
                    text: qsTr("Puffer ") + Player.cacheSeconds.toFixed(0) + qsTr(" s")
                    color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.mono
                }
                Text {
                    text: win.vinfo.displayFps > 0 ? qsTr("Anzeige ") + win.vinfo.displayFps.toFixed(3) + qsTr(" Hz") : ""
                    color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.mono
                }
                // Update verfügbar: ein Klick lädt, prüft und installiert
                Text {
                    visible: Updater.available || Updater.busy
                    text: Updater.busy ? Updater.status + (Updater.progress > 0 ? " " + Math.round(Updater.progress * 100) + " %" : "")
                                       : (Updater.canInstall ? qsTr("Lumen %1 verfügbar – jetzt aktualisieren") : qsTr("Lumen %1 verfügbar – Download-Seite")).arg(Updater.latestVersion)
                    color: Theme.accent
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { enabled: !Updater.busy; onTapped: Updater.canInstall ? Updater.install() : Updater.openPage() }
                }
                Text { text: qsTr("Lumen ") + Qt.application.version; color: Theme.textFaint; font.pixelSize: 11 }
            }
        }
    }

    // Hinweis, solange etwas über das Fenster gezogen wird
    Rectangle {
        anchors.fill: parent
        visible: dropArea.containsDrag
        color: "#d00a0b0e"
        border.color: Theme.accent
        border.width: 2
        Text {
            anchors.centerIn: parent
            text: qsTr("Hier ablegen zum Abspielen")
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
    }
}
