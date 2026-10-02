import QtQuick
import QtQuick.Layouts

// Reiter eines StackLayout, der erst beim ersten Anzeigen erzeugt wird (und dann bleibt).
// Verwendung: LazyPane { sourceComponent: Component { CinemaPane {} } }
Loader {
    readonly property bool wanted: StackLayout.isCurrentItem
    active: false
    onWantedChanged: if (wanted) active = true
    Component.onCompleted: if (wanted) active = true
}
