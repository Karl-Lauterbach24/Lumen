import QtQuick
import Lumen.Core

// Mediathek: erst die Orte (interner Speicher, USB, Netzlaufwerke), darunter ihre Ordner. Jede
// Ebene ist eine eigene Seite – Zurück führt in den Ordner darüber, an die Stelle von vorhin.
OsPage {
    id: lib
    property string path: ""       // leer = die Orte
    property string name: ""
    property var listing: null     // null = wird gelesen

    icon: path === "" ? "folder" : ""
    kicker: path === "" ? "" : qsTr("Mediathek")
    title: path === "" ? qsTr("Mediathek") : name
    busy: path !== "" && listing === null
    note: path === "" && Os.places.length <= 1 ? qsTr("Filme liegen im internen Speicher, auf einem angeschlossenen USB-Datenträger oder auf einem Netzlaufwerk (Einstellungen › Netzlaufwerke).")
        : (listing !== null && listing.length === 0) ? qsTr("Hier liegt nichts, was sich abspielen lässt.") : ""

    Component.onCompleted: if (path !== "") Os.listFolder(path)
    Connections {
        target: Os
        function onFolderListed(folder, entries) { if (folder === lib.path) lib.listing = entries }
    }

    function placeIcon(kind) { return kind === "network" ? "network" : kind === "disc" ? "disc" : kind === "usb" ? "usb" : "storage" }
    function open(entry) {
        if (entry.dir) os.push(libraryPage, { path: entry.path, name: entry.name })
        else os.openPath(entry.path)
    }

    entries: {
        if (path === "")
            return Os.places.map(p => ({
                label: p.name || qsTr("Interner Speicher"), icon: lib.placeIcon(p.kind),
                detail: p.total > 0 ? qsTr("%1 frei").arg(os.bytes(p.free)) : "",
                run: () => os.push(libraryPage, { path: p.path, name: p.name || qsTr("Interner Speicher") })
            }))
        return (listing || []).map(e => ({
            label: e.name,
            icon: e.dir ? "folder" : e.kind === "file" ? "film" : "disc",
            detail: e.dir ? "" : e.kind === "file" ? os.bytes(e.size) : e.kind === "image" ? qsTr("Disc-Abbild") : os.kindLabel(e.kind),
            run: () => lib.open(e)
        }))
    }
}
