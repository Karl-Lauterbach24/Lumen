import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// Eine Seite von LumenOS: links, worum es geht (Symbol, Titel, Erklärung), rechts eine Liste von
// Einträgen, die mit den Pfeiltasten gewählt und mit OK ausgelöst werden. Die meisten Seiten sind
// nur das.
//   entries: [{label, detail, icon, dimmed, checked, run: function}]
FocusScope {
    id: page
    property string title
    property string icon: ""        // großes Symbol über dem Titel
    property string kicker: ""      // kleine Zeile über dem Titel (z. B. „Einrichtung · Schritt 2 von 6“)
    property string note            // Erklärung unter dem Titel
    property color noteColor: Theme.textDim
    property string footer          // Hinweis am unteren Rand der linken Spalte
    property var entries: []
    property bool busy: false
    property real progress: -1      // 0..1: Balken unter der Erklärung, < 0 = keiner
    // Die gewählte Zeile gehört der Seite, nicht der Liste: die Liste finge nach jedem Umbau (ein Schalter
    // kippt, ein Auftrag ist fertig) wieder oben an.
    property int currentIndex: 0
    readonly property real u: os.u

    function activate(i) {
        const e = entries[i]
        if (e && e.run && !e.dimmed && !busy) e.run()
    }

    function select(i) {
        // (eine leere Liste – ein Auftrag läuft – vergisst die Stelle nicht)
        if (entries.length > 0)
            currentIndex = Math.max(0, Math.min(i, entries.length - 1))
        list.currentIndex = currentIndex
    }
    onCurrentIndexChanged: list.currentIndex = currentIndex
    onEntriesChanged: Qt.callLater(() => select(currentIndex))

    // Zurück innerhalb der Seite (einen Schritt, nicht die Seite verlassen): liefert true, wenn erledigt
    property var backHandler: null

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            if (!(backHandler && backHandler())) os.back()
            event.accepted = true
        }
        else if (event.key === Qt.Key_Home) { os.home(); event.accepted = true }
    }

    // ---------------------------------------------------------------- links
    Item {
        id: left
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.leftMargin: 96 * page.u
        anchors.topMargin: 190 * page.u
        anchors.bottomMargin: 70 * page.u
        width: parent.width * 0.33

        Column {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            spacing: 22 * page.u
            Rectangle {
                visible: page.icon.length > 0
                width: 112 * page.u; height: width; radius: width / 2
                color: Qt.rgba(0.48, 0.58, 1, 0.16)
                border.width: 1; border.color: Qt.rgba(0.48, 0.58, 1, 0.35)
                Image {
                    anchors.centerIn: parent
                    source: page.icon ? Os.icon(page.icon) : ""
                    sourceSize.width: 58 * page.u; sourceSize.height: 58 * page.u
                }
            }
            Text {
                visible: text.length > 0
                width: parent.width
                text: page.kicker
                color: Theme.accent
                font.family: Theme.font; font.pixelSize: 21 * page.u; font.weight: Font.DemiBold
                font.letterSpacing: 2.2 * page.u; font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: page.title
                color: Theme.text
                font.family: Theme.font; font.pixelSize: 56 * page.u; font.weight: Font.DemiBold
                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                lineHeight: 1.08
            }
            Text {
                visible: text.length > 0
                width: parent.width
                text: page.note
                color: page.noteColor
                font.family: Theme.font; font.pixelSize: 25 * page.u
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            }
            // Fortschritt
            Rectangle {
                visible: page.progress >= 0
                width: parent.width; height: 8 * page.u; radius: height / 2
                color: Qt.rgba(1, 1, 1, 0.1)
                Rectangle {
                    height: parent.height; radius: height / 2
                    width: parent.width * Math.max(0.02, Math.min(1, page.progress))
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: 300 } }
                }
            }
            // „es geschieht etwas“: drei Punkte
            Row {
                visible: page.busy
                spacing: 12 * page.u
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        width: 14 * page.u; height: width; radius: width / 2
                        color: Theme.accent
                        SequentialAnimation on opacity {
                            running: page.busy; loops: Animation.Infinite
                            PauseAnimation { duration: index * 180 }
                            NumberAnimation { from: 0.25; to: 1; duration: 380 }
                            NumberAnimation { from: 1; to: 0.25; duration: 380 }
                            PauseAnimation { duration: (2 - index) * 180 }
                        }
                    }
                }
            }
        }
        Text {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            visible: text.length > 0
            text: page.footer
            color: Theme.textFaint
            font.family: Theme.font; font.pixelSize: 21 * page.u
            wrapMode: Text.WordWrap; lineHeight: 1.25
        }
    }

    // ---------------------------------------------------------------- rechts
    ListView {
        id: list
        anchors.left: left.right; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.leftMargin: 72 * page.u
        anchors.rightMargin: 96 * page.u
        anchors.topMargin: 190 * page.u
        anchors.bottomMargin: 70 * page.u
        focus: true
        clip: true
        spacing: 14 * page.u
        // Die Liste weiß nur, wie viele Zeilen es sind; jede Zeile liest ihren Stand aus page.entries.
        // Ändert sich der Stand bei gleicher Länge, wird so nichts neu aufgebaut.
        model: page.entries.length
        highlightMoveDuration: 140
        preferredHighlightBegin: height * 0.2
        preferredHighlightEnd: height * 0.8
        highlightRangeMode: ListView.ApplyRange
        keyNavigationEnabled: false
        onCountChanged: Qt.callLater(() => page.select(page.currentIndex))
        delegate: OsItem {
            required property int index
            readonly property var entry: page.entries[index] || ({})
            width: list.width
            u: page.u
            label: entry.label || ""
            detail: entry.detail || ""
            iconName: entry.icon || ""
            dimmed: !!entry.dimmed
            checked: entry.checked
            // (nicht activeFocus: das hängt daran, ob das Fenster gerade das aktive des Systems ist)
            current: page.currentIndex === index && page.StackView.status === StackView.Active
            onClicked: { page.select(index); page.activate(index) }
        }
        Keys.onUpPressed: page.select(page.currentIndex - 1)
        Keys.onDownPressed: page.select(page.currentIndex + 1)
        Keys.onReturnPressed: page.activate(page.currentIndex)
        Keys.onEnterPressed: page.activate(page.currentIndex)
        Keys.onSelectPressed: page.activate(page.currentIndex)
        Keys.onSpacePressed: page.activate(page.currentIndex)
    }
}
