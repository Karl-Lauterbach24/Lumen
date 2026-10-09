import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// LumenOS: die Oberfläche von Lumen als Abspielgerät ("lumen --os"). Ein Fenster, bildschirmfüllend,
// mit Pfeiltasten, OK und Zurück zu bedienen – von der Tastatur wie von jeder eingerichteten
// Fernbedienung (deren Tasten kommen hier als dieselben Tasten an). Das Bild läuft im Player-Fenster,
// das sich für die Dauer der Wiedergabe davorlegt.
Window {
    id: os
    width: 1280
    height: 720
    visible: true
    visibility: Os.kiosk ? Window.FullScreen : Window.Windowed
    title: qsTr("Lumen")
    color: Theme.bg

    // Maße für den Blick vom Sofa: alles in Teilen der Bildhöhe (1 = ein Bildpunkt bei 1080 Zeilen)
    readonly property real u: height / 1080
    readonly property color focusColor: "#4d66e8"
    property string currentDevice: ""
    readonly property var disc: {
        for (let i = 0; i < Drives.drives.length; ++i) {
            const d = Drives.drives[i]
            if (d.optical && d.hasDisc && d.kind) return d
        }
        return null
    }
    readonly property string discName: !disc ? "" : (Disc.info.device === disc.path && Disc.info.discName) ? Disc.info.discName : (disc.label || disc.kindLabel)

    Settings {
        id: settings
        category: "ui"
        property bool autoPlay: true
        property bool startWithMenu: true
    }
    Settings {
        id: osSettings
        category: "os"
        property bool setupDone: false
        property string setupStage: ""
        // Aktualisierungen auf einem Stick, die schon angeboten wurden (Pfad und Version)
        property string offeredUpdates: ""
    }

    function push(component, properties) { stack.push(component, properties || {}) }
    function back() { if (stack.depth > 1) stack.pop() }
    function home() { stack.pop(null) }
    function openPage(name) {
        const pages = { disc: discPage, library: libraryPage, settings: settingsPage, power: powerPage }
        push(pages[name], name === "library" ? { path: "" } : {})
    }
    // Die Disc, die läuft, hat das Laufwerk verlassen (die Taste am Laufwerk, ein abgezogenes Laufwerk):
    // die Wiedergabe endet – sonst liefe der Film aus dem Zwischenspeicher weiter, bis der leer ist.
    property bool playingDisc: false
    function isDiscPath(path) {
        for (let i = 0; i < Drives.drives.length; ++i) {
            const d = Drives.drives[i]
            if (d.optical && d.hasDisc && d.path === path) return true
        }
        return false
    }
    Connections {
        target: Drives
        function onDrivesChanged() {
            if (!os.playingDisc || Player.idle || os.isDiscPath(os.currentDevice)) return
            os.playingDisc = false
            Player.stop()
            Disc.clear()
        }
    }
    function openPath(path, withMenu) {
        currentDevice = path
        playingDisc = isDiscPath(path)
        const kind = Player.detectKind(path)
        if (kind === "dcp") { Dcp.open(path, true); return }
        if (kind !== "file") Disc.scan(path); else Disc.clear()
        Player.openSource(path, (withMenu === undefined ? settings.startWithMenu : withMenu) ? "menu" : "main", -1)
    }
    function playDisc(withMenu) { if (disc) openPath(disc.path, withMenu) }
    function eject() {
        if (!disc) return
        const device = disc.device
        if (currentDevice === disc.path) Player.stop()
        Disc.clear()
        // eine eingehängte Disc gibt nur das System frei
        if (Os.system) Os.admin(["eject", device]); else Drives.eject(device)
        home()
    }
    // Text mit der Bildschirmtastatur erfragen; done(text) bei „Fertig“
    function ask(title, text, password, done) { push(keyboardPage, { title: title, text: text || "", password: !!password, done: done }) }
    function bytes(n) {
        if (!(n > 0)) return ""
        const units = ["B", "KB", "MB", "GB", "TB"]
        let i = 0
        while (n >= 1000 && i < units.length - 1) { n /= 1000; ++i }
        return n.toFixed(n >= 100 || i < 2 ? 0 : 1) + " " + units[i]
    }
    function kindLabel(k) {
        return ({ bluray: qsTr("Blu-ray"), dvd: qsTr("DVD-Video"), hddvd: qsTr("HD DVD"), vcd: qsTr("Video-CD"), svcd: qsTr("Super Video-CD"),
                  cdda: qsTr("Audio-CD"), dcp: qsTr("Digital Cinema Package") })[k] || ""
    }
    // Zeilen „a<Tab>b<Tab>c“ des Hilfsprogramms -> Objekte mit den genannten Feldern
    function rows(text, names) {
        return text.split("\n").filter(l => l.length > 0).map(l => {
            const f = l.split("\t"), o = {}
            for (let i = 0; i < names.length; ++i) o[names[i]] = f[i] || ""
            return o
        })
    }
    function pairs(text) {
        const o = {}
        text.split("\n").forEach(l => { const i = l.indexOf("="); if (i > 0) o[l.slice(0, i)] = l.slice(i + 1) })
        return o
    }
    function lastLine(out, err) { return ((err || "").trim() || (out || "").trim()).split("\n").pop() }

    // ------------------------------------------------------------------ Einrichtung beim ersten Start
    // Von einem Stick gestartet: die Platten dieses Geräts, auf die LumenOS könnte
    property var installDisks: []
    readonly property var setupOrder: ["welcome", "remote", "install", "network", "update", "discs"]
    function setupApplies(stage) {
        if (stage === "install") return Os.live && installDisks.length > 0
        if (stage === "network") return Os.system && Os.info.addresses.length === 0
        if (stage === "update") return Os.system && !Os.live
        if (stage === "discs") return Os.system
        return true
    }
    readonly property var setupSteps: setupOrder.filter(s => setupApplies(s) || s === osSettings.setupStage)
    function setupKicker(stage) {
        const i = setupSteps.indexOf(stage)
        return qsTr("Einrichtung · Schritt %1 von %2").arg(i + 1).arg(setupSteps.length)
    }
    function setupShow(stage) {
        while (stage && stage !== "done" && !setupApplies(stage))
            stage = setupOrder[setupOrder.indexOf(stage) + 1] || "done"
        stack.pop(null, StackView.Immediate)
        if (!stage || stage === "done") {
            osSettings.setupDone = true
            osSettings.setupStage = ""
            return
        }
        osSettings.setupStage = stage
        const pages = { welcome: setupWelcome, remote: setupRemote, install: installPage, network: networkPage, update: setupUpdate, discs: discSupportPage }
        push(pages[stage], { setup: true })
    }
    function setupNext() { setupShow(setupOrder[setupOrder.indexOf(osSettings.setupStage) + 1] || "done") }
    function setupStart() {
        osSettings.setupDone = false
        setupShow(osSettings.setupStage || "welcome")
    }
    Component.onCompleted: {
        const begin = () => { if (!osSettings.setupDone) setupStart() }
        if (Os.system && Os.live)
            Os.admin(["install-list"], (code, out) => { installDisks = rows(out, ["device", "size", "name", "bus"]).map(d => { d.size = Number(d.size); return d }); begin() })
        else
            Qt.callLater(begin)
    }

    // ------------------------------------------------------------------ Aktualisierung von einem Stick
    property var offlineUpdates: []
    function scanOffline(offer) {
        if (!Os.system) return
        Os.admin(["offline-scan"], (code, out) => {
            offlineUpdates = rows(out, ["kind", "version", "path", "note"])
            if (!offer || !Player.idle || Remotes.wizardActive || !osSettings.setupDone) return
            // einmal anbieten, was neu dazugekommen ist und sich einspielen lässt
            const fresh = offlineUpdates.filter(c => (c.note === "newer" || c.note === "interface") && osSettings.offeredUpdates.indexOf(c.path + "|" + c.version) < 0)
            if (fresh.length === 0) return
            osSettings.offeredUpdates = offlineUpdates.map(c => c.path + "|" + c.version).join("\n")
            push(offlinePage)
        })
    }
    Timer { id: offlineTimer; interval: 2500; onTriggered: os.scanOffline(true) }
    Connections {
        target: Os
        function onPlacesChanged() { offlineTimer.restart() }
    }

    Connections {
        target: Drives
        function onDiscInserted(drive) {
            // (während kopiert wird, gehört das Laufwerk MakeMKV)
            if (Plugins.discsHeld || Rip.running) return
            if (settings.autoPlay && Player.idle && !Remotes.wizardActive && osSettings.setupDone) os.openPath(drive.path)
            else if (drive.kind !== "dcp") Disc.scan(drive.path)
        }
    }
    // Ein Eingabegerät ohne Zuordnung: einrichten, sobald nichts läuft
    Connections {
        target: Remotes
        function onSetupWanted(id, name) {
            if (!Player.idle || Remotes.wizardActive) return
            Remotes.startWizard(id)
            if (Remotes.wizardActive) os.push(remoteSetupPage)
        }
    }
    // Nach der Wiedergabe gehört die Tastatur wieder dieser Oberfläche
    Connections {
        target: Player
        function onIdleChanged() { if (Player.idle) { os.requestActivate(); if (stack.currentItem) stack.currentItem.forceActiveFocus() } }
    }

    // ------------------------------------------------------------------ Hintergrund und Kopf
    Rectangle { anchors.fill: parent; color: "#0a0c11" }
    // Ein Verlauf von oben nach unten mit zwei weichen Lichtflecken; das Programm rechnet ihn je Bildpunkt
    // und rundet mit etwas Rauschen (OsBackdrop). Aus halb durchsichtigen Farben gemalt, zeigt ein so
    // dunkler Verlauf Ringe – auf einem Fernseher gut zu sehen.
    Image {
        anchors.fill: parent
        source: "image://lumenos/backdrop"
        sourceSize: Qt.size(os.width, os.height)
        smooth: true
        asynchronous: true
    }

    StackView {
        id: stack
        anchors.fill: parent
        focus: true
        initialItem: homePage
        onCurrentItemChanged: if (currentItem) currentItem.forceActiveFocus()
        pushEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 }
            NumberAnimation { property: "x"; from: 60 * os.u; to: 0; duration: 200; easing.type: Easing.OutCubic }
        }
        pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 } }
        popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } }
        popExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 }
            NumberAnimation { property: "x"; from: 0; to: 60 * os.u; duration: 160; easing.type: Easing.InCubic }
        }
    }

    // Kopf: Zeichen und Name links, Stand rechts
    Row {
        anchors.left: parent.left; anchors.top: parent.top
        anchors.leftMargin: 96 * os.u; anchors.topMargin: 72 * os.u
        spacing: 18 * os.u
        Image {
            source: "qrc:/qt/qml/Lumen/resources/logo/lumen-emblem-small.png"
            height: 44 * os.u; width: height * 192 / 91
            fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: qsTr("LUMEN")
            color: Theme.text
            font.family: Theme.font; font.pixelSize: 28 * os.u; font.weight: Font.Bold; font.letterSpacing: 8 * os.u
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    Row {
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: 96 * os.u; anchors.topMargin: 76 * os.u
        spacing: 16 * os.u
        Rectangle {
            visible: Os.update.state === "installing" || Os.update.state === "downloading"
            height: 46 * os.u; width: updText.implicitWidth + 44 * os.u; radius: height / 2
            color: Qt.rgba(0.95, 0.72, 0.29, 0.16)
            Text { id: updText; anchors.centerIn: parent; text: qsTr("Aktualisierung läuft"); color: Theme.warn; font.family: Theme.font; font.pixelSize: 22 * os.u }
        }
        Rectangle {
            height: 46 * os.u; width: netRow.implicitWidth + 40 * os.u; radius: height / 2
            color: Qt.rgba(1, 1, 1, 0.06)
            Row {
                id: netRow
                anchors.centerIn: parent
                spacing: 10 * os.u
                Image { source: Os.icon("network"); sourceSize.width: 24 * os.u; sourceSize.height: 24 * os.u; opacity: Os.info.addresses.length > 0 ? 0.8 : 0.35; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: Os.info.addresses.length > 0 ? Os.info.addresses[0] : qsTr("kein Netz")
                    color: Theme.textDim; font.family: Theme.font; font.pixelSize: 22 * os.u
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        Rectangle {
            height: 46 * os.u; width: clock.implicitWidth + 40 * os.u; radius: height / 2
            color: Qt.rgba(1, 1, 1, 0.06)
            Text {
                id: clock
                anchors.centerIn: parent
                color: Theme.text; font.family: Theme.font; font.pixelSize: 22 * os.u; font.weight: Font.DemiBold
                function tick() { text = Qt.formatTime(new Date(), "hh:mm") }
                Component.onCompleted: tick()
                Timer { interval: 10000; running: true; repeat: true; onTriggered: clock.tick() }
            }
        }
    }
    // Ohne Grafiktreiber (Os.softwareGraphics) zeigte der Bildschirm ein einzelnes neues Bild – die Uhr,
    // eine Disc, die das Laufwerk verlassen hat – manchmal erst, wenn ein weiteres folgte: Qt hatte es
    // übergeben, der Compositor es bestätigt, zu sehen war es nicht. Ein Bildpunkt in der dunklen Ecke
    // wechselt darum jede Sekunde unmerklich seine Farbe; was aussteht, kommt damit auf den Schirm.
    Rectangle {
        visible: Os.softwareGraphics
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: 1
        height: 1
        color: beat.on ? "#07080c" : "#07080b"
        Timer { id: beat; property bool on: false; interval: 1000; repeat: true; running: Os.softwareGraphics; onTriggered: on = !on }
    }
    // Der Mauszeiger verschwindet, wenn die Maus ruht (auf dem Fernseher stört er)
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        property bool resting: true
        property real atX: -1
        property real atY: -1
        cursorShape: resting ? Qt.BlankCursor : Qt.ArrowCursor
        // Nur eine Maus, die sich wirklich bewegt hat: Qt meldet die Stelle des Zeigers auch dann neu,
        // wenn sich unter ihm etwas ändert (die Uhr, eine neue Seite) – und die erste Meldung sagt nur,
        // wo er gerade steht.
        onPositionChanged: mouse => {
            if (Math.abs(mouse.x - atX) < 3 && Math.abs(mouse.y - atY) < 3)
                return
            const first = atX < 0
            atX = mouse.x
            atY = mouse.y
            if (first)
                return
            resting = false
            rest.restart()
        }
        Timer { id: rest; interval: 3000; onTriggered: parent.resting = true }
    }

    // ------------------------------------------------------------------ Start
    Component { id: homePage; OsHome {} }

    // ------------------------------------------------------------------ Disc
    Component {
        id: discPage
        OsPage {
            readonly property var info: os.disc && Disc.info.device === os.disc.path ? Disc.info : ({})
            icon: "disc"
            kicker: os.disc ? os.disc.kindLabel : ""
            title: os.disc ? os.discName : qsTr("Disc")
            busy: Disc.busy
            note: !os.disc ? qsTr("Es liegt keine Disc im Laufwerk.")
                : Disc.busy ? qsTr("Lese Disc-Struktur …")
                : info.error ? info.error
                : (info.aacsDetected && !info.aacsHandled) ? qsTr("Diese Disc ist verschlüsselt. Unter Einstellungen › Discs steht, was dafür eingerichtet werden kann.")
                : ""
            noteColor: (info.error || (info.aacsDetected && !info.aacsHandled)) ? Theme.warn : Theme.textDim
            Component.onCompleted: if (os.disc && Disc.info.device !== os.disc.path && !Disc.busy) Disc.scan(os.disc.path)
            entries: {
                const list = []
                if (!os.disc) return list
                const path = os.disc.path
                list.push({ label: qsTr("Mit Disc-Menü abspielen"), icon: "menu", run: () => os.openPath(path, true) })
                list.push({ label: qsTr("Hauptfilm abspielen"), icon: "play", run: () => os.openPath(path, false) })
                const titles = info.titles || []
                for (let i = 0; i < titles.length; ++i) {
                    const t = titles[i]
                    if (t.playlist === undefined) continue
                    list.push({ label: qsTr("Titel %1").arg(t.index + 1) + (t.main ? "  ·  " + qsTr("Hauptfilm") : ""),
                                detail: Theme.time(t.duration) + (t.video ? "  ·  " + t.video : ""),
                                icon: "film", run: () => { os.currentDevice = path; os.playingDisc = os.isDiscPath(path); Player.openPlaylist(path, t.playlist) } })
                }
                if (Rip.available)
                    list.push({ label: qsTr("Auf den Speicher kopieren …"), detail: Rip.running ? Rip.status : "", icon: "download", run: () => os.push(ripPage) })
                list.push({ label: qsTr("Auswerfen"), icon: "eject", run: () => os.eject() })
                return list
            }
        }
    }

    // ------------------------------------------------------------------ Kopieren (MakeMKV)
    Component {
        id: ripPage
        OsPage {
            icon: "download"
            title: qsTr("Disc kopieren")
            busy: Rip.running
            progress: Rip.running ? Rip.progress : -1
            note: Rip.running ? Rip.status
                : Rip.lastError ? Rip.lastError
                : Rip.status ? Rip.status
                : qsTr("Kopiert die Filme der Disc mit MakeMKV als MKV-Dateien in einen Ordner der Mediathek. Das dauert je nach Disc zwischen zehn Minuten und einer Stunde; das Laufwerk gehört so lange dem Kopieren.")
            noteColor: Rip.lastError && !Rip.running ? Theme.bad : Theme.textDim
            entries: {
                const list = []
                if (Rip.running) {
                    list.push({ label: qsTr("Abbrechen"), icon: "stop", run: () => Rip.cancel() })
                    return list
                }
                for (let i = 0; i < Os.places.length; ++i) {
                    const p = Os.places[i]
                    if (p.kind === "disc") continue
                    list.push({ label: qsTr("Nach „%1“ kopieren").arg(p.name || qsTr("Interner Speicher")),
                                detail: qsTr("%1 frei").arg(os.bytes(p.free)), icon: p.kind === "network" ? "network" : p.kind === "usb" ? "usb" : "storage",
                                run: () => { Player.stop(); Rip.start(os.disc ? os.disc.device : "", p.path, os.discName) } })
                }
                return list
            }
        }
    }

    // ------------------------------------------------------------------ Mediathek
    Component { id: libraryPage; OsLibrary {} }

    // ------------------------------------------------------------------ Einstellungen
    Component {
        id: settingsPage
        OsPage {
            icon: "tune"
            title: qsTr("Einstellungen")
            Component.onCompleted: os.scanOffline(false)
            entries: (os.installDisks.length > 0 ? [{ label: qsTr("Auf diesem Gerät installieren"), icon: "storage", detail: qsTr("läuft gerade vom Stick"), run: () => os.push(installPage) }] : []).concat([
                { label: qsTr("Fernbedienungen"), icon: "remote", detail: Remotes.devices.filter(d => d.mapped).map(d => d.name).join(", "), run: () => os.push(remotesPage) },
                { label: qsTr("Netzwerk"), icon: "wifi", detail: Os.info.addresses.join(", "), dimmed: !Os.system, run: () => os.push(networkPage) },
                { label: qsTr("Netzlaufwerke"), icon: "network", dimmed: !Os.system, run: () => os.push(sharesPage) },
                { label: qsTr("Bluetooth"), icon: "bluetooth", dimmed: !Os.system, run: () => os.push(bluetoothPage) },
                { label: qsTr("Wiedergabe"), icon: "play", detail: Profiles.current.name || "", run: () => os.push(playbackPage) },
                { label: qsTr("Sprache"), icon: "language", detail: I18n.languages.find(l => l.code === I18n.effective).name, run: () => os.push(languagePage) },
                { label: qsTr("Discs"), icon: "disc", dimmed: !Os.system, run: () => os.push(discSupportPage) },
                { label: qsTr("Aktualisierung"), icon: "download", detail: qsTr("Lumen %1").arg(Os.info.version), dimmed: !Os.system, run: () => os.push(updatePage) },
                { label: qsTr("Über dieses Gerät"), icon: "info", detail: Os.info.hostname, run: () => os.push(aboutPage) },
                { label: qsTr("Einrichtung erneut durchlaufen"), icon: "sparkle", run: () => { osSettings.setupStage = "welcome"; os.setupStart() } }
            ])
            footer: Os.system ? "" : qsTr("Grau: nur auf einem Gerät mit LumenOS.")
        }
    }

    // ------------------------------------------------------------------ Auf die interne Platte
    Component {
        id: installPage
        OsPage {
            id: inst
            property bool setup: false
            property var chosen: null
            property string result: ""
            property bool failed: false
            icon: "storage"
            // einmal bestimmt: mit dem Installieren rückt die Einrichtung schon einen Schritt weiter (siehe run)
            Component.onCompleted: kicker = setup ? os.setupKicker("install") : ""
            // Zurück aus der Nachfrage führt zur Wahl der Platte; während installiert wird, nirgendwohin
            backHandler: () => { if (busy) return true; if (chosen && !result) { chosen = null; return true } return setup }
            title: qsTr("LumenOS installieren")
            note: result ? result
                : busy ? qsTr("Installiere auf %1 … Das dauert einige Minuten. Das Gerät dabei nicht ausschalten.").arg(chosen.name)
                : chosen ? qsTr("Alles auf „%1“ (%2) wird gelöscht – auch Filme und andere Systeme darauf. Das lässt sich nicht zurücknehmen.").arg(chosen.name).arg(os.bytes(chosen.size))
                : qsTr("LumenOS läuft gerade von einem Stick oder einer Disc. Auf einer Platte dieses Geräts startet es schneller, merkt sich alles und hat Platz für Filme. Wähle die Platte.")
            noteColor: failed ? Theme.bad : (chosen && !busy && !result) ? Theme.warn : Theme.textDim
            function run() {
                busy = true
                // Die Einrichtung geht auf der Platte weiter, wo sie hier aufhört
                const stage = osSettings.setupStage
                if (setup) { osSettings.setupStage = "network"; osSettings.sync() }
                Os.admin(["install-to", chosen.device], (code, out, err) => {
                    busy = false
                    failed = code !== 0
                    if (failed && setup) osSettings.setupStage = stage
                    result = code === 0 ? qsTr("Fertig. Den Stick abziehen und neu starten: LumenOS startet dann von „%1“.").arg(chosen.name)
                                        : qsTr("Die Installation ist gescheitert: %1").arg(os.lastLine(out, err))
                })
            }
            entries: result ? (failed ? [{ label: qsTr("Zurück"), run: () => { result = ""; failed = false; chosen = null } }]
                                      : [{ label: qsTr("Neu starten"), icon: "refresh", run: () => Os.admin(["power", "reboot"]) }, { label: qsTr("Später"), run: () => inst.setup ? os.setupNext() : os.home() }])
                : busy ? []
                : chosen ? [{ label: qsTr("Abbrechen"), run: () => chosen = null },
                            { label: qsTr("Platte löschen und installieren"), icon: "warning", run: () => inst.run() }]
                : os.installDisks.map(d => ({ label: d.name, icon: "storage", detail: os.bytes(d.size) + (d.bus ? "  ·  " + d.bus.toUpperCase() : ""), run: () => chosen = d }))
                    .concat(inst.setup ? [{ label: qsTr("Nur ausprobieren, nichts installieren"), icon: "play", run: () => os.setupNext() }] : [])
        }
    }

    // ------------------------------------------------------------------ Einrichtung: Begrüßung und Sprache
    Component {
        id: setupWelcome
        OsPage {
            property bool setup: true
            icon: "sparkle"
            kicker: os.setupKicker("welcome")
            title: qsTr("Willkommen")
            note: qsTr("Lumen spielt deine Discs und Filme. Ein paar Fragen, dann kann es losgehen – zuerst die Sprache.")
            backHandler: () => true
            Component.onCompleted: currentIndex = Math.max(0, I18n.languages.findIndex(l => l.code === I18n.effective))
            entries: I18n.languages.map(l => ({ label: l.name, icon: "language", detail: l.code === I18n.effective ? qsTr("aktiv") : "",
                                                run: () => { I18n.language = l.code; os.setupNext() } }))
            footer: qsTr("Pfeiltasten wählen, OK öffnet, Zurück geht eine Seite zurück.")
        }
    }
    Component {
        id: setupRemote
        OsPage {
            property bool setup: true
            icon: "remote"
            kicker: os.setupKicker("remote")
            title: qsTr("Fernbedienung")
            note: qsTr("Tastatur und Maus funktionieren sofort. Eine Fernbedienung oder ein Gamepad jetzt anschließen und eine Taste darauf drücken: Lumen fragt dann ab, welche Taste was tun soll.")
            backHandler: () => { os.setupShow("welcome"); return true }
            entries: Remotes.devices.map(d => ({
                    label: d.name, icon: "remote", detail: d.mapped ? qsTr("eingerichtet") : qsTr("neu"),
                    run: () => { Remotes.startWizard(d.id); if (Remotes.wizardActive) os.push(remoteSetupPage) } }))
                .concat(Os.system ? [{ label: qsTr("Bluetooth-Gerät koppeln"), icon: "bluetooth", run: () => os.push(bluetoothPage) }] : [])
                .concat([{ label: qsTr("Weiter"), icon: "right", run: () => os.setupNext() }])
            Component.onCompleted: currentIndex = entries.length - 1
        }
    }
    // Einrichtung: Lumen auf den neuesten Stand bringen, wenn ein Netz da ist
    Component {
        id: setupUpdate
        OsPage {
            id: su
            property bool setup: true
            property double began: 0
            property bool online: true
            readonly property var s: Os.update
            readonly property bool fresh: s.time !== undefined && s.time >= began
            readonly property bool installing: fresh && s.state === "installing"
            icon: "download"
            kicker: os.setupKicker("update")
            title: qsTr("Aktualisierung")
            busy: online && (!fresh || s.state === "checking" || s.state === "downloading" || s.state === "installing" || s.state === "waiting")
            // Meldet sich der Dienst nicht (GitHub nicht erreichbar, Netz nur dem Namen nach da), geht es ohne ihn weiter
            Timer { interval: 45000; running: su.online && !su.fresh; onTriggered: su.online = false }
            note: !online ? qsTr("Kein Netz: Lumen bleibt, wie es ist. Aktualisieren lässt es sich später – von selbst, sobald ein Netz da ist, oder von einem USB-Stick.")
                : !fresh || s.state === "checking" ? qsTr("Suche nach einer neuen Version …")
                : s.state === "downloading" ? qsTr("Lade Lumen %1 …").arg(s.latest)
                : s.state === "installing" ? qsTr("Installiere Lumen %1 … Lumen startet danach neu.").arg(s.latest)
                : s.state === "error" ? qsTr("Aktualisierung gescheitert: %1").arg(s.message)
                : qsTr("Lumen %1 ist aktuell.").arg(Os.info.version)
            noteColor: fresh && s.state === "error" ? Theme.bad : Theme.textDim
            backHandler: () => true
            Component.onCompleted: {
                began = Math.floor(Date.now() / 1000) - 1
                Os.admin(["setup-info"], (code, out) => {
                    online = os.pairs(out).online === "yes"
                    if (online) Os.admin(["update-now"])
                })
            }
            // Weiter geht es jederzeit – ein Paket lädt im Hintergrund zu Ende und wird eingespielt, sobald
            // nichts läuft. Nur während Lumen ersetzt wird, gibt es nichts zu wählen (es startet gleich neu).
            entries: installing ? [] : [{ label: qsTr("Weiter"), icon: "right", run: () => os.setupNext() }]
        }
    }

    Component {
        id: remotesPage
        OsPage {
            icon: "remote"
            title: qsTr("Fernbedienungen")
            note: Remotes.devices.length === 0
                ? qsTr("Tastatur und Maus brauchen keine Einrichtung. Eine Fernbedienung oder ein Gamepad anschließen – per USB, Bluetooth oder HDMI-CEC –, dann erscheint es hier, und Lumen fragt seine Tasten ab.")
                : qsTr("OK richtet ein Gerät neu ein.")
            entries: {
                const list = Remotes.devices.map(d => ({
                    label: d.name, icon: "remote",
                    detail: !d.connected ? qsTr("nicht verbunden") : d.mapped ? qsTr("eingerichtet") : qsTr("neu"),
                    dimmed: !d.connected,
                    run: () => { Remotes.startWizard(d.id); if (Remotes.wizardActive) os.push(remoteSetupPage) }
                }))
                const mapped = Remotes.devices.filter(d => d.mapped)
                if (mapped.length > 0)
                    list.push({ label: qsTr("Alle Zuordnungen löschen"), icon: "close", run: () => mapped.forEach(d => Remotes.forget(d.id)) })
                return list
            }
        }
    }
    Component { id: remoteSetupPage; OsRemoteSetup {} }
    Component { id: keyboardPage; OsKeyboard {} }

    Component {
        id: playbackPage
        OsPage {
            icon: "play"
            title: qsTr("Wiedergabe")
            entries: {
                const list = [
                    { label: qsTr("Disc beim Einlegen abspielen"), checked: settings.autoPlay, run: () => settings.autoPlay = !settings.autoPlay },
                    { label: qsTr("Mit Disc-Menü starten"), checked: settings.startWithMenu, run: () => settings.startWithMenu = !settings.startWithMenu }
                ]
                for (let i = 0; i < Profiles.profiles.length; ++i) {
                    const p = Profiles.profiles[i]
                    list.push({ label: qsTr("Ausgabeprofil: %1").arg(p.name), icon: "screen", detail: p.id === Profiles.currentId ? qsTr("aktiv") : "", run: () => Profiles.currentId = p.id })
                }
                return list
            }
            footer: qsTr("Ausgabeprofile legen Bild und Ton fest: HDR, Durchleitung an den Receiver, 3D. Angelegt und geändert werden sie in Lumen auf einem Rechner; hier wird eines gewählt.")
        }
    }
    Component {
        id: languagePage
        OsPage {
            icon: "language"
            title: qsTr("Sprache")
            Component.onCompleted: currentIndex = Math.max(0, I18n.languages.findIndex(l => l.code === I18n.effective))
            entries: I18n.languages.map(l => ({ label: l.name, detail: l.code === I18n.effective ? qsTr("aktiv") : "", run: () => { I18n.language = l.code; os.back() } }))
        }
    }

    // ------------------------------------------------------------------ Netzwerk
    Component {
        id: networkPage
        OsPage {
            id: net
            property bool setup: false
            property var networks: []
            property string message: ""
            readonly property bool online: Os.info.addresses.length > 0
            icon: "wifi"
            kicker: setup ? os.setupKicker("network") : ""
            title: qsTr("Netzwerk")
            note: message ? message : online ? qsTr("Verbunden: %1").arg(Os.info.addresses.join(", "))
                : setup ? qsTr("Mit einem Netz hält sich Lumen selbst aktuell und erreicht Netzlaufwerke. Ein Netzwerkkabel wird von selbst erkannt; für WLAN ein Netz wählen.")
                : qsTr("Nicht verbunden. Ein Netzwerkkabel wird von selbst erkannt.")
            backHandler: () => setup
            function scan() {
                busy = true
                Os.admin(["wifi-list"], (code, out) => {
                    busy = false
                    networks = os.rows(out, ["ssid", "signal", "security", "active"]).map(n => { n.active = n.active === "yes"; return n })
                })
            }
            function connect(n, password) {
                busy = true
                message = qsTr("Verbinde mit „%1“ …").arg(n.ssid)
                Os.admin(["wifi-connect", n.ssid, password || ""], (code, out, err) => {
                    busy = false
                    message = code === 0 ? "" : qsTr("Verbindung mit „%1“ gescheitert. Passwort prüfen.").arg(n.ssid)
                    Os.refresh()
                    scan()
                })
            }
            Component.onCompleted: scan()
            entries: {
                const list = []
                if (setup) list.push({ label: online ? qsTr("Weiter") : qsTr("Ohne Netz weiter"), icon: "right", run: () => os.setupNext() })
                networks.forEach(n => list.push({
                    label: n.ssid, icon: "wifi",
                    detail: (n.active ? qsTr("verbunden") + "  ·  " : "") + n.signal + " %" + (n.security && n.security !== "--" ? "  ·  " + n.security : ""),
                    run: () => (n.security && n.security !== "--" && !n.active) ? os.ask(qsTr("Passwort für „%1“").arg(n.ssid), "", true, text => net.connect(n, text)) : net.connect(n, "")
                }))
                list.push({ label: qsTr("Erneut suchen"), icon: "refresh", run: () => net.scan() })
                return list
            }
        }
    }

    // ------------------------------------------------------------------ Netzlaufwerke
    Component {
        id: sharesPage
        OsPage {
            id: shares
            property var list: []
            property string message: ""
            icon: "network"
            title: qsTr("Netzlaufwerke")
            note: message ? message : qsTr("Freigaben eines NAS oder Rechners (SMB oder NFS). Sie erscheinen in der Mediathek und stehen dem Kopieren als Ziel zur Verfügung.")
            function load() {
                Os.admin(["share-list"], (code, out) => { list = os.rows(out, ["name", "type", "source", "mounted"]) })
            }
            // Fragt Schritt für Schritt: Rechner, Freigabe, Nutzer, Passwort
            function add(type) {
                os.ask(qsTr("Name oder Adresse des Rechners"), "", false, host => {
                    if (!host) return
                    os.ask(type === "nfs" ? qsTr("Pfad der Freigabe, z. B. /volume1/filme") : qsTr("Name der Freigabe, z. B. filme"), "", false, share => {
                        if (!share) return
                        const finish = (user, password) => {
                            shares.busy = true
                            shares.message = qsTr("Verbinde mit %1 …").arg(host)
                            const name = (share.replace(/^\/+/, "").replace(/[^A-Za-z0-9_-]+/g, "-") || "share") + "-" + host.replace(/[^A-Za-z0-9_-]+/g, "-")
                            Os.admin(["share-add", name, type, host, share, user, password], (code, out, err) => {
                                shares.busy = false
                                shares.message = code === 0 ? "" : qsTr("Die Freigabe ließ sich nicht einhängen: %1").arg(os.lastLine(out, err))
                                shares.load()
                            })
                        }
                        if (type === "nfs") return finish("", "")
                        os.ask(qsTr("Nutzername (leer = Gast)"), "", false, user => {
                            if (!user) return finish("", "")
                            os.ask(qsTr("Passwort für %1").arg(user), "", true, password => finish(user, password))
                        })
                    })
                })
            }
            Component.onCompleted: load()
            entries: {
                const out = list.map(s => ({
                    label: s.name, icon: "folder", detail: s.source + (s.mounted === "yes" ? "" : "  ·  " + qsTr("nicht erreichbar")),
                    run: () => os.push(shareRemovePage, { share: s.name, done: () => shares.load() })
                }))
                out.push({ label: qsTr("Windows-/NAS-Freigabe hinzufügen (SMB)"), icon: "network", run: () => shares.add("smb") })
                out.push({ label: qsTr("NFS-Freigabe hinzufügen"), icon: "network", run: () => shares.add("nfs") })
                return out
            }
        }
    }
    Component {
        id: shareRemovePage
        OsPage {
            property string share
            property var done
            icon: "network"
            title: share
            entries: [
                { label: qsTr("Behalten"), run: () => os.back() },
                { label: qsTr("Entfernen"), icon: "close", run: () => { busy = true; Os.admin(["share-remove", share], () => { busy = false; if (done) done(); os.back() }) } }
            ]
        }
    }

    // ------------------------------------------------------------------ Bluetooth
    Component {
        id: bluetoothPage
        OsPage {
            id: bt
            property var found: []
            property string message: ""
            icon: "bluetooth"
            title: qsTr("Bluetooth")
            note: message ? message : qsTr("Das Gerät in den Kopplungsmodus bringen (meist: eine Taste einige Sekunden halten), dann suchen. Fernbedienungen und Gamepads fragt Lumen nach dem Koppeln nach ihren Tasten.")
            function scan() {
                busy = true
                message = qsTr("Suche Geräte …")
                Os.admin(["bt-scan", "10"], (code, out, err) => {
                    busy = false
                    message = code === 0 ? "" : qsTr("Bluetooth steht nicht zur Verfügung.")
                    found = os.rows(out, ["mac", "name", "paired", "connected"])
                })
            }
            function pair(d) {
                busy = true
                message = (d.paired === "yes" ? qsTr("Entferne „%1“ …") : qsTr("Koppele „%1“ …")).arg(d.name)
                Os.admin([d.paired === "yes" ? "bt-remove" : "bt-pair", d.mac], (code, out, err) => {
                    busy = false
                    message = code === 0 ? "" : qsTr("Koppeln gescheitert: %1").arg(os.lastLine(out, err))
                    Remotes.rescan()
                    scan()
                })
            }
            Component.onCompleted: scan()
            entries: {
                const list = found.map(d => ({
                    label: d.name || d.mac, icon: "bluetooth",
                    detail: d.connected === "yes" ? qsTr("verbunden") + "  ·  " + qsTr("OK entfernt") : d.paired === "yes" ? qsTr("gekoppelt") + "  ·  " + qsTr("OK entfernt") : qsTr("OK koppelt"),
                    run: () => bt.pair(d)
                }))
                list.push({ label: qsTr("Erneut suchen"), icon: "refresh", run: () => bt.scan() })
                return list
            }
        }
    }

    // ------------------------------------------------------------------ Discs: was für verschlüsselte dazukommt
    // In der Einrichtung wird gewählt und dann mit „Weiter“ eingerichtet; in den Einstellungen schaltet
    // jeder Eintrag sofort.
    Component {
        id: discSupportPage
        OsPage {
            id: support
            property bool setup: false
            property var state: ({})
            property string message: ""
            property bool failed: false
            // Einrichtung: was gewählt ist
            property bool wantKeys: false
            property bool wantDvd: false
            property bool wantMakemkv: false
            icon: "disc"
            kicker: setup ? os.setupKicker("discs") : ""
            title: setup ? qsTr("Verschlüsselte Discs") : qsTr("Discs")
            note: message ? message
                : qsTr("Unverschlüsselte Discs spielt Lumen von sich aus. Fast alle gekauften Blu-rays und DVDs sind verschlüsselt: Hier entscheidest du, was dieses Gerät dafür bekommt. Ob das Umgehen eines Kopierschutzes erlaubt ist, auch nur zum Abspielen eigener Discs, hängt von deinem Land ab.")
            noteColor: failed ? Theme.bad : Theme.textDim
            backHandler: () => busy || setup
            function load() { Os.admin(["support-status"], (code, out) => { state = os.pairs(out) }) }
            function run(verb, args, text, then) {
                busy = true
                failed = false
                message = text
                Os.admin([verb].concat(args || []), (code, out, err) => {
                    busy = false
                    failed = code !== 0
                    message = failed ? os.lastLine(out, err) : ""
                    load()
                    if (then) then(code === 0)
                })
            }
            // Einrichtung: das Gewählte der Reihe nach
            function apply() {
                const jobs = []
                if (wantKeys && state.keydb !== "yes") jobs.push(["keydb-fetch", qsTr("Lade die Schlüsseldatenbank …")])
                if (wantDvd && state.dvdcss !== "yes") jobs.push(["install-dvdcss", qsTr("Richte libdvdcss ein …")])
                if (wantMakemkv && !state.makemkv) {
                    jobs.push(["install-makemkv", qsTr("Lade und baue MakeMKV … (zehn Minuten und mehr)")])
                    jobs.push(["makemkv-betakey", qsTr("Lese den Schlüssel aus dem MakeMKV-Forum …")])
                }
                const errors = []
                const step = () => {
                    if (jobs.length === 0) {
                        if (errors.length === 0) return os.setupNext()
                        failed = true
                        message = errors.join("\n")
                        wantKeys = wantDvd = wantMakemkv = false
                        return
                    }
                    const job = jobs.shift()
                    run(job[0], [], job[1], ok => { if (!ok) errors.push(message); step() })
                }
                step()
            }
            Component.onCompleted: load()
            entries: busy ? [] : setup ? [
                { label: qsTr("Blu-ray: Schlüsseldatenbank laden"), icon: "key", checked: wantKeys || state.keydb === "yes", run: () => wantKeys = !wantKeys },
                { label: qsTr("DVD: libdvdcss einrichten"), icon: "disc", checked: wantDvd || state.dvdcss === "yes", run: () => wantDvd = !wantDvd },
                { label: qsTr("MakeMKV laden und bauen"), icon: "download", checked: wantMakemkv || !!state.makemkv, run: () => wantMakemkv ? wantMakemkv = false : os.push(makemkvPage, { install: () => support.wantMakemkv = true }) },
                { label: (wantKeys || wantDvd || wantMakemkv) ? qsTr("Einrichten und weiter") : qsTr("Weiter ohne"), icon: "right", run: () => (wantKeys || wantDvd || wantMakemkv) ? support.apply() : os.setupNext() }
            ] : [
                { label: qsTr("Blu-ray: Schlüsseldatenbank"), icon: "key", checked: state.keydb === "yes",
                  detail: state.keydb_date || "",
                  run: () => state.keydb === "yes" ? support.run("keydb-remove", [], qsTr("Entferne die Schlüsseldatenbank …")) : support.run("keydb-fetch", [], qsTr("Lade die Schlüsseldatenbank …")) },
                { label: qsTr("Blu-ray: eigene Schlüsseldatei von USB übernehmen"), icon: "usb",
                  run: () => support.run("keydb-import", Os.places.filter(p => p.kind === "usb").map(p => p.path + "/KEYDB.cfg"), qsTr("Suche KEYDB.cfg auf den USB-Datenträgern …")) },
                { label: qsTr("DVD: libdvdcss"), icon: "disc", checked: state.dvdcss === "yes",
                  run: () => state.dvdcss === "yes" ? support.run("remove-dvdcss", [], qsTr("Entferne libdvdcss …")) : support.run("install-dvdcss", [], qsTr("Richte libdvdcss ein …")) },
                { label: state.makemkv ? qsTr("MakeMKV neu einrichten") : qsTr("MakeMKV einrichten (Abspielen und Kopieren)"), icon: "download",
                  detail: state.makemkv ? state.makemkv : "",
                  run: () => os.push(makemkvPage, { install: () => support.run("install-makemkv", [], qsTr("Lade und baue MakeMKV … (zehn Minuten und mehr)")) }) },
                { label: qsTr("MakeMKV: Beta-Schlüssel aus dessen Forum eintragen"), icon: "key", dimmed: !state.makemkv,
                  run: () => support.run("makemkv-betakey", [], qsTr("Lese den Schlüssel aus dem MakeMKV-Forum …")) },
                { label: qsTr("MakeMKV: gekauften Schlüssel eingeben"), icon: "key", dimmed: !state.makemkv,
                  run: () => os.ask(qsTr("MakeMKV-Schlüssel"), "", false, key => support.run("makemkv-key", [key.trim()], qsTr("Trage den Schlüssel ein …"))) }
            ]
            footer: qsTr("Die Schlüsseldatenbank pflegt eine Gemeinschaft im Netz; Lumen lädt sie von dort und hält sie wöchentlich aktuell. MakeMKV ist ein Programm von GuinpinSoft mit eigener Lizenz.")
        }
    }
    Component {
        id: makemkvPage
        OsPage {
            property var install
            icon: "download"
            title: qsTr("MakeMKV einrichten")
            note: qsTr("MakeMKV ist kein Teil von Lumen. Es gehört GuinpinSoft und hat eine eigene Lizenz (www.makemkv.com/eula), die du mit dem Einrichten annimmst. Während der Beta-Phase ist es mit dem Schlüssel aus seinem Forum kostenlos nutzbar. Lumen lädt es von makemkv.com und baut es auf diesem Gerät; das dauert zehn Minuten und mehr und braucht eine Netzverbindung.")
            entries: [
                { label: qsTr("Abbrechen"), run: () => os.back() },
                { label: qsTr("Lizenz annehmen und einrichten"), icon: "check", run: () => { const f = install; os.back(); if (f) f() } }
            ]
        }
    }

    // ------------------------------------------------------------------ Aktualisierung
    Component {
        id: updatePage
        OsPage {
            id: upd
            property string auto: ""
            readonly property var s: Os.update
            icon: "download"
            title: qsTr("Aktualisierung")
            note: s.state === "checking" ? qsTr("Suche nach einer neuen Version …")
                : s.state === "downloading" ? qsTr("Lade Lumen %1 …").arg(s.latest)
                : s.state === "waiting" ? qsTr("Lumen %1 ist geladen und wird installiert, sobald nichts mehr läuft.").arg(s.latest)
                : s.state === "installing" ? qsTr("Installiere Lumen %1 … Lumen startet danach neu.").arg(s.latest)
                : s.state === "error" ? qsTr("Aktualisierung gescheitert: %1").arg(s.message)
                : s.latest && s.latest !== Os.info.version ? qsTr("Lumen %1 ist da. Installiert ist %2.").arg(s.latest).arg(Os.info.version)
                : qsTr("Lumen %1 ist aktuell.").arg(Os.info.version)
            noteColor: s.state === "error" ? Theme.bad : Theme.textDim
            busy: s.state === "checking" || s.state === "downloading" || s.state === "installing"
            Component.onCompleted: { Os.admin(["update-auto", "status"], (code, out) => auto = out.trim()); os.scanOffline(false) }
            entries: [
                { label: qsTr("Jetzt suchen und installieren"), icon: "refresh", run: () => Os.admin(["update-now"]) },
                { label: qsTr("Von selbst aktualisieren"), checked: auto === "on",
                  run: () => Os.admin(["update-auto", auto === "on" ? "off" : "on"], () => Os.admin(["update-auto", "status"], (code, out) => auto = out.trim())) },
                { label: qsTr("Von einem USB-Stick aktualisieren"), icon: "usb", detail: os.offlineUpdates.length > 0 ? qsTr("gefunden") : "", run: () => os.push(offlinePage) }
            ]
            footer: qsTr("Lumen wird aus der neuesten Veröffentlichung auf GitHub aktualisiert, geprüft gegen deren Prüfsummen, und nur, während nichts läuft. Das System darunter holt seine Sicherheitsupdates selbst.")
                    + (Os.overlayVersion ? "\n" + qsTr("Oberfläche aus dem Quelltext: %1").arg(Os.overlayVersion) : "")
        }
    }
    // Aktualisierung ohne Netz: ein Paket, ein Zip mit Paketen oder der Quelltext auf einem Stick
    Component {
        id: offlinePage
        OsPage {
            id: off
            property string message: ""
            property bool failed: false
            icon: "usb"
            title: qsTr("Aktualisierung vom Stick")
            note: message ? message
                : os.offlineUpdates.length === 0 ? qsTr("Auf einen USB-Stick gehört eines davon, in den obersten Ordner: das Paket „Lumen-…-linux-….deb“ oder „LumenOS-update-….zip“ von der Seite der Veröffentlichung – oder der Quelltext, wie GitHub ihn als Zip ausgibt (Code › Download ZIP). Der Quelltext bringt Oberfläche, Übersetzungen und den Systemteil; der Kern des Players kommt nur mit einem Paket.")
                : qsTr("Das liegt auf dem Stick. OK spielt es ein; Lumen startet danach neu.")
            noteColor: failed ? Theme.bad : Theme.textDim
            backHandler: () => busy
            function label(c) {
                if (c.kind === "source") return c.note === "interface" ? qsTr("Oberfläche und Systemteil aus dem Quelltext (%1)").arg(c.version)
                                                                       : qsTr("Quelltext %1 – braucht ein neueres Paket").arg(c.version)
                return c.note === "older" ? qsTr("Lumen %1 – älter als das installierte").arg(c.version)
                     : c.note === "same" ? qsTr("Lumen %1 erneut installieren").arg(c.version)
                     : qsTr("Lumen %1 installieren").arg(c.version)
            }
            function install(c) {
                busy = true
                failed = false
                message = qsTr("Spiele ein: %1 …").arg(c.path.split("/").pop())
                Os.admin(["offline-install", c.path], (code, out, err) => {
                    busy = false
                    failed = code !== 0
                    message = failed ? os.lastLine(out, err) : qsTr("Eingespielt. Lumen startet neu.")
                })
            }
            Component.onCompleted: os.scanOffline(false)
            entries: busy ? [] : os.offlineUpdates.map(c => ({
                    label: off.label(c), icon: c.kind === "source" ? "sparkle" : "download",
                    detail: c.path.split("/").slice(2, 3).join(""),
                    dimmed: c.note === "older" || c.note === "needs-package",
                    run: () => off.install(c) }))
                .concat([{ label: qsTr("Erneut suchen"), icon: "refresh", run: () => os.scanOffline(false) }])
        }
    }

    Component {
        id: aboutPage
        OsPage {
            icon: "info"
            title: qsTr("Über dieses Gerät")
            entries: {
                const list = [
                    { label: qsTr("Lumen"), detail: Os.info.version },
                    { label: qsTr("System"), detail: Os.info.system },
                    { label: qsTr("Name im Netz"), detail: Os.info.hostname },
                    { label: qsTr("Adresse"), detail: Os.info.addresses.join(", ") || qsTr("kein Netz") },
                    { label: qsTr("Grafik"), detail: Player.hardware.renderer }
                ]
                for (let i = 0; i < Os.places.length; ++i) {
                    const p = Os.places[i]
                    list.push({ label: p.name || qsTr("Interner Speicher"), detail: qsTr("%1 frei von %2").arg(os.bytes(p.free)).arg(os.bytes(p.total)) })
                }
                return list
            }
        }
    }

    Component {
        id: powerPage
        OsPage {
            icon: "power"
            title: qsTr("Ausschalten")
            entries: [
                { label: qsTr("Ausschalten"), icon: "power", dimmed: !Os.system, run: () => Os.admin(["power", "poweroff"]) },
                { label: qsTr("Neu starten"), icon: "refresh", dimmed: !Os.system, run: () => Os.admin(["power", "reboot"]) },
                { label: qsTr("Nur Lumen neu starten"), icon: "loop", run: () => Plugins.restartApp() },
                { label: qsTr("Abbrechen"), run: () => os.back() }
            ]
        }
    }
}
