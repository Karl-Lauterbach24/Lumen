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

    function push(component, properties) { stack.push(component, properties || {}) }
    function back() { if (stack.depth > 1) stack.pop() }
    function home() { stack.pop(null) }
    function openPath(path, withMenu) {
        currentDevice = path
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
    // Text mit der Bildschirmtastatur erfragen; done(text) bei OK
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

    Connections {
        target: Drives
        function onDiscInserted(drive) {
            // (während kopiert wird, gehört das Laufwerk MakeMKV)
            if (Plugins.discsHeld || Rip.running) return
            if (settings.autoPlay && Player.idle && !Remotes.wizardActive) os.openPath(drive.path)
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

    StackView {
        id: stack
        anchors.fill: parent
        focus: true
        initialItem: homePage
        onCurrentItemChanged: if (currentItem) currentItem.forceActiveFocus()
        pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130 } }
        pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
        popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130 } }
        popExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
    }

    // Uhr und Netz, auf jeder Seite oben rechts
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 80 * os.u
        anchors.topMargin: 78 * os.u
        spacing: 24 * os.u
        Text {
            visible: Os.update.state === "installing" || Os.update.state === "downloading"
            text: qsTr("Aktualisierung läuft")
            color: Theme.warn; font.pixelSize: 24 * os.u
        }
        Text {
            text: Os.info.addresses.length > 0 ? Os.info.addresses[0] : qsTr("kein Netz")
            color: Theme.textFaint; font.pixelSize: 24 * os.u
        }
        Text {
            id: clock
            color: Theme.textDim; font.pixelSize: 24 * os.u
            function tick() { text = Qt.formatTime(new Date(), "hh:mm") }
            Component.onCompleted: tick()
            Timer { interval: 10000; running: true; repeat: true; onTriggered: clock.tick() }
        }
    }

    // ------------------------------------------------------------------ Start
    Component {
        id: homePage
        OsPage {
            title: qsTr("Lumen")
            note: Player.lastError ? Player.lastError : ""
            noteColor: Theme.bad
            entries: {
                const list = []
                if (os.disc)
                    list.push({ label: qsTr("%1 abspielen").arg(os.discName), detail: os.disc.kindLabel, icon: "play", run: () => os.playDisc() })
                list.push({ label: qsTr("Disc"), detail: os.disc ? os.discName : qsTr("keine eingelegt"), icon: "disc", run: () => os.push(discPage) })
                list.push({ label: qsTr("Mediathek"), detail: Os.places.map(p => p.name || qsTr("Interner Speicher")).join("  ·  "), icon: "folder", run: () => os.push(libraryPage, { path: "" }) })
                if (Recent.items.length > 0)
                    list.push({ label: qsTr("Zuletzt gespielt"), detail: Recent.items[0].title, icon: "loop", run: () => os.push(recentPage) })
                list.push({ label: qsTr("Einstellungen"), icon: "tune", run: () => os.push(settingsPage) })
                list.push({ label: qsTr("Ausschalten"), icon: "close", run: () => os.push(powerPage) })
                return list
            }
            footer: qsTr("Pfeiltasten wählen, OK öffnet, Zurück geht eine Seite zurück.")
        }
    }

    // ------------------------------------------------------------------ Disc
    Component {
        id: discPage
        OsPage {
            readonly property var info: os.disc && Disc.info.device === os.disc.path ? Disc.info : ({})
            title: os.disc ? os.discName : qsTr("Disc")
            busy: Disc.busy
            note: !os.disc ? qsTr("Es liegt keine Disc im Laufwerk.")
                : Disc.busy ? qsTr("Lese Disc-Struktur …")
                : info.error ? info.error
                : (info.aacsDetected && !info.aacsHandled) ? qsTr("Diese Disc ist verschlüsselt. Lumen entschlüsselt nicht selbst: unter Einstellungen › Discs steht, was dafür eingerichtet werden kann.")
                : os.disc.kindLabel
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
                    list.push({ label: qsTr("Titel %1").arg(t.index + 1) + (t.main ? " · " + qsTr("Hauptfilm") : ""),
                                detail: Theme.time(t.duration) + (t.video ? "  ·  " + t.video : ""),
                                icon: "film", run: () => { os.currentDevice = path; Player.openPlaylist(path, t.playlist) } })
                }
                if (Rip.available)
                    list.push({ label: qsTr("Auf den Speicher kopieren …"), detail: Rip.running ? Rip.status : "", icon: "folder", run: () => os.push(ripPage) })
                list.push({ label: qsTr("Auswerfen"), icon: "eject", run: () => os.eject() })
                return list
            }
        }
    }

    // ------------------------------------------------------------------ Kopieren (MakeMKV)
    Component {
        id: ripPage
        OsPage {
            title: qsTr("Disc kopieren")
            busy: Rip.running
            note: Rip.running ? Rip.status
                : Rip.lastError ? Rip.lastError
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
                                detail: qsTr("%1 frei").arg(os.bytes(p.free)), icon: "folder",
                                run: () => { Player.stop(); Rip.start(os.disc ? os.disc.device : "", p.path) } })
                }
                return list
            }
        }
    }

    // ------------------------------------------------------------------ Mediathek
    Component { id: libraryPage; OsLibrary {} }

    Component {
        id: recentPage
        OsPage {
            title: qsTr("Zuletzt gespielt")
            entries: Recent.items.map(item => ({
                label: item.title, icon: item.kind === "file" ? "film" : "disc",
                detail: item.kind === "file" && item.duration > 0 ? Theme.time(item.position) + " / " + Theme.time(item.duration) : os.kindLabel(item.kind),
                run: () => os.openPath(item.path)
            }))
        }
    }

    // ------------------------------------------------------------------ Einstellungen
    Component {
        id: settingsPage
        OsPage {
            id: prefs
            title: qsTr("Einstellungen")
            // Von einem Stick gestartet: die Platten dieses Geräts, auf die LumenOS könnte
            property var disks: []
            Component.onCompleted: if (Os.system) Os.admin(["install-list"], (code, out) => {
                disks = out.split("\n").filter(l => l.length > 0).map(l => { const f = l.split("\t"); return { device: f[0], size: Number(f[1]), name: f[2], bus: f[3] } })
            })
            entries: (disks.length > 0 ? [{ label: qsTr("Auf diesem Gerät installieren"), icon: "folder", detail: qsTr("läuft gerade vom Stick"), run: () => os.push(installPage, { disks: prefs.disks }) }] : []).concat([
                { label: qsTr("Fernbedienungen"), icon: "tune", detail: Remotes.devices.filter(d => d.mapped).map(d => d.name).join(", "), run: () => os.push(remotesPage) },
                { label: qsTr("Netzwerk"), icon: "link", detail: Os.info.addresses.join(", "), dimmed: !Os.system, run: () => os.push(networkPage) },
                { label: qsTr("Netzlaufwerke"), icon: "folder", dimmed: !Os.system, run: () => os.push(sharesPage) },
                { label: qsTr("Bluetooth"), icon: "cast", dimmed: !Os.system, run: () => os.push(bluetoothPage) },
                { label: qsTr("Wiedergabe"), icon: "play", detail: Profiles.current.name || "", run: () => os.push(playbackPage) },
                { label: qsTr("Discs"), icon: "disc", dimmed: !Os.system, run: () => os.push(discSupportPage) },
                { label: qsTr("Aktualisierung"), icon: "refresh", detail: qsTr("Lumen %1").arg(Os.info.version), dimmed: !Os.system, run: () => os.push(updatePage) },
                { label: qsTr("Über dieses Gerät"), icon: "info", detail: Os.info.hostname, run: () => os.push(aboutPage) }
            ])
            footer: Os.system ? "" : qsTr("Grau: nur auf einem Gerät mit LumenOS.")
        }
    }

    // ------------------------------------------------------------------ Auf die interne Platte
    Component {
        id: installPage
        OsPage {
            id: inst
            property var disks: []
            property var chosen: null
            property string result: ""
            property bool failed: false
            // Zurück aus der Nachfrage führt zur Wahl der Platte; während installiert wird, nirgendwohin
            backHandler: () => { if (busy) return true; if (chosen && !result) { chosen = null; return true } return false }
            title: qsTr("LumenOS installieren")
            note: result ? result
                : busy ? qsTr("Installiere auf %1 … Das dauert einige Minuten. Das Gerät dabei nicht ausschalten.").arg(chosen.name)
                : chosen ? qsTr("Alles auf „%1“ (%2) wird gelöscht – auch Filme und andere Systeme darauf. Das lässt sich nicht zurücknehmen.").arg(chosen.name).arg(os.bytes(chosen.size))
                : qsTr("LumenOS läuft gerade von einem Stick oder einer Disc. Auf einer Platte dieses Geräts startet es schneller, merkt sich alles und hat Platz für Filme. Wähle die Platte.")
            noteColor: failed ? Theme.bad : (chosen && !busy && !result) ? Theme.warn : Theme.textDim
            function run() {
                busy = true
                Os.admin(["install-to", chosen.device], (code, out, err) => {
                    busy = false
                    failed = code !== 0
                    result = code === 0 ? qsTr("Fertig. Den Stick abziehen und neu starten: LumenOS startet dann von „%1“.").arg(chosen.name)
                                        : qsTr("Die Installation ist gescheitert: %1").arg((err || out).trim().split("\n").pop())
                })
            }
            entries: result ? (failed ? [{ label: qsTr("Zurück"), run: () => os.back() }]
                                      : [{ label: qsTr("Neu starten"), icon: "refresh", run: () => Os.admin(["power", "reboot"]) }, { label: qsTr("Später"), run: () => os.home() }])
                : busy ? []
                : chosen ? [{ label: qsTr("Abbrechen"), run: () => chosen = null },
                            { label: qsTr("Platte löschen und installieren"), icon: "warning", run: () => inst.run() }]
                : disks.map(d => ({ label: d.name, icon: "folder", detail: os.bytes(d.size) + (d.bus ? "  ·  " + d.bus.toUpperCase() : ""), run: () => chosen = d }))
        }
    }

    Component {
        id: remotesPage
        OsPage {
            title: qsTr("Fernbedienungen")
            note: Remotes.devices.length === 0
                ? qsTr("Tastatur und Maus brauchen keine Einrichtung. Eine Fernbedienung oder ein Gamepad anschließen – per USB, Bluetooth oder HDMI-CEC –, dann erscheint es hier, und Lumen fragt seine Tasten ab.")
                : qsTr("OK richtet ein Gerät neu ein.")
            entries: {
                const list = Remotes.devices.map(d => ({
                    label: d.name, icon: "tune",
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
            title: qsTr("Wiedergabe")
            entries: {
                const list = [
                    { label: qsTr("Disc beim Einlegen abspielen"), detail: settings.autoPlay ? qsTr("ja") : qsTr("nein"), run: () => settings.autoPlay = !settings.autoPlay },
                    { label: qsTr("Mit Disc-Menü starten"), detail: settings.startWithMenu ? qsTr("ja") : qsTr("nein"), run: () => settings.startWithMenu = !settings.startWithMenu },
                    { label: qsTr("Sprache"), detail: I18n.languages.find(l => l.code === I18n.effective).name, run: () => os.push(languagePage) }
                ]
                for (let i = 0; i < Profiles.profiles.length; ++i) {
                    const p = Profiles.profiles[i]
                    list.push({ label: qsTr("Ausgabeprofil: %1").arg(p.name), detail: p.id === Profiles.currentId ? qsTr("aktiv") : "", run: () => Profiles.currentId = p.id })
                }
                return list
            }
            footer: qsTr("Ausgabeprofile legen Bild und Ton fest: HDR, Durchleitung an den Receiver, 3D. Angelegt und geändert werden sie in Lumen auf einem Rechner; hier wird eines gewählt.")
        }
    }
    Component {
        id: languagePage
        OsPage {
            title: qsTr("Sprache")
            entries: I18n.languages.map(l => ({ label: l.name, detail: l.code === I18n.effective ? qsTr("aktiv") : "", run: () => { I18n.language = l.code; os.back() } }))
        }
    }

    // ------------------------------------------------------------------ Netzwerk
    Component {
        id: networkPage
        OsPage {
            id: net
            title: qsTr("Netzwerk")
            property var networks: []
            property string message: ""
            note: message ? message : Os.info.addresses.length > 0 ? qsTr("Verbunden: %1").arg(Os.info.addresses.join(", ")) : qsTr("Nicht verbunden. Ein Netzwerkkabel wird von selbst erkannt.")
            function scan() {
                busy = true
                Os.admin(["wifi-list"], (code, out) => {
                    busy = false
                    networks = out.split("\n").filter(l => l.length > 0).map(l => { const f = l.split("\t"); return { ssid: f[0], signal: f[1], security: f[2], active: f[3] === "yes" } })
                })
            }
            function connect(n, password) {
                busy = true
                message = qsTr("Verbinde mit „%1“ …").arg(n.ssid)
                Os.admin(["wifi-connect", n.ssid, password || ""], (code, out, err) => {
                    busy = false
                    message = code === 0 ? "" : qsTr("Verbindung mit „%1“ gescheitert. Passwort prüfen.").arg(n.ssid)
                    scan()
                })
            }
            Component.onCompleted: scan()
            entries: {
                const list = networks.map(n => ({
                    label: n.ssid, icon: "link",
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
            title: qsTr("Netzlaufwerke")
            property var list: []
            property string message: ""
            note: message ? message : list.length === 0 ? qsTr("Freigaben eines NAS oder Rechners (SMB oder NFS). Sie erscheinen in der Mediathek und stehen dem Kopieren als Ziel zur Verfügung.") : ""
            function load() {
                Os.admin(["share-list"], (code, out) => {
                    list = out.split("\n").filter(l => l.length > 0).map(l => { const f = l.split("\t"); return { name: f[0], type: f[1], source: f[2], mounted: f[3] === "yes" } })
                })
            }
            // Fragt Schritt für Schritt: Rechner, Freigabe, Nutzer, Passwort
            function add(type) {
                os.ask(qsTr("Name oder Adresse des Rechners"), "", false, host => {
                    os.ask(type === "nfs" ? qsTr("Pfad der Freigabe, z. B. /volume1/filme") : qsTr("Name der Freigabe, z. B. filme"), "", false, share => {
                        const finish = (user, password) => {
                            shares.busy = true
                            shares.message = qsTr("Verbinde mit %1 …").arg(host)
                            const name = (share.replace(/^\/+/, "").replace(/[^A-Za-z0-9_-]+/g, "-") || "share") + "-" + host.replace(/[^A-Za-z0-9_-]+/g, "-")
                            Os.admin(["share-add", name, type, host, share, user, password], (code, out, err) => {
                                shares.busy = false
                                shares.message = code === 0 ? "" : qsTr("Die Freigabe ließ sich nicht einhängen: %1").arg((err || out).trim().split("\n").pop())
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
                    label: s.name, icon: "folder", detail: s.source + (s.mounted ? "" : "  ·  " + qsTr("nicht erreichbar")),
                    run: () => os.push(shareRemovePage, { share: s.name, done: () => shares.load() })
                }))
                out.push({ label: qsTr("Windows-/NAS-Freigabe hinzufügen (SMB)"), icon: "link", run: () => shares.add("smb") })
                out.push({ label: qsTr("NFS-Freigabe hinzufügen"), icon: "link", run: () => shares.add("nfs") })
                return out
            }
        }
    }
    Component {
        id: shareRemovePage
        OsPage {
            property string share
            property var done
            title: share
            entries: [
                { label: qsTr("Entfernen"), icon: "close", run: () => { busy = true; Os.admin(["share-remove", share], () => { busy = false; if (done) done(); os.back() }) } },
                { label: qsTr("Behalten"), run: () => os.back() }
            ]
        }
    }

    // ------------------------------------------------------------------ Bluetooth
    Component {
        id: bluetoothPage
        OsPage {
            id: bt
            title: qsTr("Bluetooth")
            property var found: []
            property string message: ""
            note: message ? message : qsTr("Das Gerät in den Kopplungsmodus bringen (meist: eine Taste einige Sekunden halten), dann suchen. Fernbedienungen und Gamepads fragt Lumen nach dem Koppeln nach ihren Tasten.")
            function scan() {
                busy = true
                message = qsTr("Suche Geräte …")
                Os.admin(["bt-scan", "10"], (code, out, err) => {
                    busy = false
                    message = code === 0 ? "" : qsTr("Bluetooth steht nicht zur Verfügung.")
                    found = out.split("\n").filter(l => l.length > 0).map(l => { const f = l.split("\t"); return { mac: f[0], name: f[1] || f[0], paired: f[2] === "yes", connected: f[3] === "yes" } })
                })
            }
            function pair(d) {
                busy = true
                message = qsTr("Koppele „%1“ …").arg(d.name)
                Os.admin([d.paired ? "bt-remove" : "bt-pair", d.mac], (code, out, err) => {
                    busy = false
                    message = code === 0 ? "" : qsTr("Koppeln gescheitert: %1").arg((err || out).trim().split("\n").pop())
                    Remotes.rescan()
                    scan()
                })
            }
            Component.onCompleted: scan()
            entries: {
                const list = found.map(d => ({
                    label: d.name, icon: "cast",
                    detail: d.connected ? qsTr("verbunden") + "  ·  " + qsTr("OK entfernt") : d.paired ? qsTr("gekoppelt") + "  ·  " + qsTr("OK entfernt") : qsTr("OK koppelt"),
                    run: () => bt.pair(d)
                }))
                list.push({ label: qsTr("Erneut suchen"), icon: "refresh", run: () => bt.scan() })
                return list
            }
        }
    }

    // ------------------------------------------------------------------ Discs: was zum Abspielen und Kopieren fehlt
    Component {
        id: discSupportPage
        OsPage {
            id: support
            title: qsTr("Discs")
            property var state: ({})
            property string message: ""
            note: message ? message
                : qsTr("Unverschlüsselte Discs spielt Lumen von sich aus. Für verschlüsselte entscheidest du, was auf diesem Gerät eingerichtet wird – Lumen bringt weder Schlüssel noch Entschlüsselung mit.")
            function load() {
                Os.admin(["support-status"], (code, out) => {
                    const s = {}
                    out.split("\n").forEach(l => { const i = l.indexOf("="); if (i > 0) s[l.slice(0, i)] = l.slice(i + 1) })
                    state = s
                })
            }
            function run(verb, args, text) {
                busy = true
                message = text
                Os.admin([verb].concat(args || []), (code, out, err) => {
                    busy = false
                    message = code === 0 ? "" : (err || out).trim().split("\n").pop()
                    load()
                })
            }
            // Schlüsseldatei (KEYDB.cfg) auf einem USB-Datenträger suchen
            function keyFiles() {
                const list = []
                for (let i = 0; i < Os.places.length; ++i)
                    if (Os.places[i].kind === "usb") list.push(Os.places[i].path + "/KEYDB.cfg")
                return list
            }
            Component.onCompleted: load()
            entries: [
                { label: qsTr("Blu-ray: Schlüsseldatei von USB übernehmen"), icon: "disc",
                  detail: state.keydb === "yes" ? qsTr("vorhanden") : qsTr("fehlt"),
                  run: () => support.run("keydb-import", keyFiles(), qsTr("Suche KEYDB.cfg auf den USB-Datenträgern …")) },
                { label: qsTr("DVD: libdvdcss einrichten"), icon: "disc",
                  detail: state.dvdcss === "yes" ? qsTr("eingerichtet") : qsTr("fehlt"), dimmed: state.dvdcss === "yes",
                  run: () => support.run("install-dvdcss", [], qsTr("Lade und baue libdvdcss … (einige Minuten)")) },
                { label: state.makemkv ? qsTr("MakeMKV neu einrichten") : qsTr("MakeMKV einrichten (Abspielen und Kopieren)"), icon: "folder",
                  detail: state.makemkv ? state.makemkv : qsTr("fehlt"),
                  run: () => os.push(makemkvPage, { install: () => support.run("install-makemkv", [], qsTr("Lade und baue MakeMKV … (zehn Minuten und mehr)")) }) },
                { label: qsTr("MakeMKV: Beta-Schlüssel aus dessen Forum eintragen"), icon: "link", dimmed: !state.makemkv,
                  run: () => support.run("makemkv-betakey", [], qsTr("Lese den Schlüssel aus dem MakeMKV-Forum …")) },
                { label: qsTr("MakeMKV: gekauften Schlüssel eingeben"), icon: "tune", dimmed: !state.makemkv,
                  run: () => os.ask(qsTr("MakeMKV-Schlüssel"), "", false, key => support.run("makemkv-key", [key.trim()], qsTr("Trage den Schlüssel ein …"))) }
            ]
            footer: qsTr("MakeMKV ist ein Programm von GuinpinSoft mit eigener Lizenz; es wird von makemkv.com geladen und auf diesem Gerät gebaut.")
        }
    }

    Component {
        id: makemkvPage
        OsPage {
            property var install
            title: qsTr("MakeMKV einrichten")
            note: qsTr("MakeMKV ist kein Teil von Lumen. Es gehört GuinpinSoft und hat eine eigene Lizenz (www.makemkv.com/eula), die du mit dem Einrichten annimmst. Während der Beta-Phase ist es mit dem Schlüssel aus seinem Forum kostenlos nutzbar. Lumen lädt es von makemkv.com und baut es auf diesem Gerät; das dauert zehn Minuten und mehr und braucht eine Netzverbindung.")
            entries: [
                { label: qsTr("Lizenz annehmen und einrichten"), icon: "folder", run: () => { const f = install; os.back(); if (f) f() } },
                { label: qsTr("Abbrechen"), run: () => os.back() }
            ]
        }
    }

    // ------------------------------------------------------------------ Aktualisierung
    Component {
        id: updatePage
        OsPage {
            id: upd
            title: qsTr("Aktualisierung")
            property string auto: ""
            readonly property var s: Os.update
            note: s.state === "checking" ? qsTr("Suche nach einer neuen Version …")
                : s.state === "downloading" ? qsTr("Lade Lumen %1 …").arg(s.latest)
                : s.state === "installing" ? qsTr("Installiere Lumen %1 …").arg(s.latest)
                : s.state === "error" ? qsTr("Aktualisierung gescheitert: %1").arg(s.message)
                : s.latest && s.latest !== Os.info.version ? qsTr("Lumen %1 ist da. Installiert ist %2.").arg(s.latest).arg(Os.info.version)
                : qsTr("Lumen %1 ist aktuell.").arg(Os.info.version)
            noteColor: s.state === "error" ? Theme.bad : Theme.textDim
            busy: s.state === "checking" || s.state === "downloading" || s.state === "installing"
            Component.onCompleted: Os.admin(["update-auto", "status"], (code, out) => auto = out.trim())
            entries: [
                { label: qsTr("Jetzt suchen und installieren"), icon: "refresh", run: () => Os.admin(["update-now"]) },
                { label: qsTr("Von selbst aktualisieren"), detail: auto === "on" ? qsTr("ja") : auto === "off" ? qsTr("nein") : "",
                  run: () => Os.admin(["update-auto", auto === "on" ? "off" : "on"], () => Os.admin(["update-auto", "status"], (code, out) => auto = out.trim())) }
            ]
            footer: qsTr("Lumen wird aus der neuesten Veröffentlichung auf GitHub aktualisiert, geprüft gegen deren Prüfsummen, und nur, während nichts läuft. Das System darunter holt seine Sicherheitsupdates selbst.")
        }
    }

    Component {
        id: aboutPage
        OsPage {
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
            title: qsTr("Ausschalten")
            entries: [
                { label: qsTr("Ausschalten"), icon: "close", dimmed: !Os.system, run: () => Os.admin(["power", "poweroff"]) },
                { label: qsTr("Neu starten"), icon: "refresh", dimmed: !Os.system, run: () => Os.admin(["power", "reboot"]) },
                { label: qsTr("Nur Lumen neu starten"), icon: "loop", run: () => Plugins.restartApp() },
                { label: qsTr("Abbrechen"), run: () => os.back() }
            ]
        }
    }
}
