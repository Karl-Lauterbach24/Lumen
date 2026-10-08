import QtQuick
import QtQuick.Layouts
import Lumen.Core

// Texteingabe ohne Tastatur: ein Tastenfeld, mit den Pfeiltasten zu bedienen. Eine angeschlossene
// Tastatur schreibt direkt.
FocusScope {
    id: kb
    property string title
    property string text: ""
    property bool password: false
    property var done              // function(text)
    property bool shift: false
    property bool symbols: false
    property bool reveal: false
    readonly property real u: os.u

    readonly property var letters: ["1234567890", "qwertzuiop", "asdfghjkl-", "yxcvbnm._@"]
    readonly property var marks: ["!\"#$%&'()*", "+,/:;<=>?[", "\\]^`{|}~€§", "äöüßÄÖÜ°·…"]
    readonly property var rows: symbols ? marks : letters
    property int row: 1
    property int column: 0
    // unterste Zeile: Umschalten, Zeichen, Leer, Löschen, Zeigen, Fertig
    readonly property var actions: ["shift", "symbols", "space", "delete", "reveal", "ok"]
    function actionLabel(a) {
        return ({ shift: "⇧", symbols: symbols ? "abc" : "#+=", space: qsTr("Leerzeichen"), delete: "⌫",
                  reveal: reveal ? qsTr("Verbergen") : qsTr("Zeigen"), ok: qsTr("Fertig") })[a]
    }
    function press() {
        if (row < 4) {
            const c = rows[row][column]
            text += shift ? c.toUpperCase() : c
            return
        }
        const a = actions[column]
        if (a === "shift") shift = !shift
        else if (a === "symbols") symbols = !symbols
        else if (a === "space") text += " "
        else if (a === "delete") text = text.slice(0, -1)
        else if (a === "reveal") reveal = !reveal
        else if (a === "ok") accept()
    }
    function accept() {
        const value = text, callback = done
        os.back()
        if (callback) callback(value)
    }
    function move(dx, dy) {
        const before = row
        row = Math.max(0, Math.min(4, row + dy))
        // zwischen den Zeichenzeilen (10 Tasten) und der Zeile der Funktionen (6 Tasten) die Spalte umrechnen
        if (before < 4 && row === 4) column = Math.round(column * 5 / 9)
        else if (before === 4 && row < 4) column = Math.round(column * 9 / 5)
        const count = row === 4 ? actions.length : 10
        column = (column + dx + count) % count
    }

    Keys.onPressed: event => {
        event.accepted = true
        if (event.key === Qt.Key_Left) move(-1, 0)
        else if (event.key === Qt.Key_Right) move(1, 0)
        else if (event.key === Qt.Key_Up) move(0, -1)
        else if (event.key === Qt.Key_Down) move(0, 1)
        // OK einer Fernbedienung (kommt als "Auswählen") setzt das gewählte Zeichen; Enter einer Tastatur,
        // mit der geschrieben wurde, schließt ab
        else if (event.key === Qt.Key_Select) press()
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { if (kb.typed) accept(); else press() }
        else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) os.back()
        else if (event.key === Qt.Key_Backspace) { text = text.slice(0, -1); typed = true }
        else if (event.text.length === 1 && event.text.charCodeAt(0) >= 32) { text += event.text; typed = true }
        else event.accepted = false
    }
    // es wurde mit einer Tastatur geschrieben
    property bool typed: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 80 * kb.u
        anchors.topMargin: 64 * kb.u
        spacing: 26 * kb.u

        Text { text: kb.title; color: Theme.text; font.pixelSize: 44 * kb.u; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92 * kb.u
            radius: 14 * kb.u
            color: Theme.raised
            border.color: Theme.accent; border.width: 2 * kb.u
            Text {
                anchors.fill: parent
                anchors.leftMargin: 28 * kb.u; anchors.rightMargin: 28 * kb.u
                verticalAlignment: Text.AlignVCenter
                text: (kb.password && !kb.reveal ? "•".repeat(kb.text.length) : kb.text) + "▏"
                color: Theme.text
                font.pixelSize: 36 * kb.u
                elide: Text.ElideLeft
            }
        }
        Item { Layout.preferredHeight: 8 * kb.u }
        Repeater {
            model: 4
            Row {
                id: line
                required property int index
                Layout.alignment: Qt.AlignHCenter
                spacing: 10 * kb.u
                Repeater {
                    model: 10
                    Rectangle {
                        required property int index
                        readonly property bool current: kb.row === line.index && kb.column === index
                        width: 104 * kb.u; height: 88 * kb.u; radius: 12 * kb.u
                        color: current ? Theme.accent : Theme.raised
                        Text {
                            anchors.centerIn: parent
                            text: { const c = kb.rows[line.index][parent.index]; return kb.shift ? c.toUpperCase() : c }
                            color: parent.current ? Theme.bg : Theme.text
                            font.pixelSize: 34 * kb.u
                        }
                        MouseArea { anchors.fill: parent; onClicked: { kb.row = line.index; kb.column = parent.index; kb.press() } }
                    }
                }
            }
        }
        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10 * kb.u
            Repeater {
                model: kb.actions
                Rectangle {
                    required property string modelData
                    required property int index
                    readonly property bool current: kb.row === 4 && kb.column === index
                    readonly property bool on: (modelData === "shift" && kb.shift) || (modelData === "symbols" && kb.symbols)
                    visible: modelData !== "reveal" || kb.password
                    width: (modelData === "space" ? 330 : modelData === "ok" ? 220 : 160) * kb.u; height: 88 * kb.u; radius: 12 * kb.u
                    color: current ? Theme.accent : on ? Theme.hover : Theme.raised
                    Text {
                        anchors.centerIn: parent
                        text: kb.actionLabel(parent.modelData)
                        color: parent.current ? Theme.bg : parent.modelData === "ok" ? Theme.accent : Theme.text
                        font.pixelSize: 28 * kb.u
                    }
                    MouseArea { anchors.fill: parent; onClicked: { kb.row = 4; kb.column = parent.index; kb.press() } }
                }
            }
        }
        Item { Layout.fillHeight: true }
        Text {
            Layout.fillWidth: true
            text: qsTr("Pfeiltasten wählen ein Zeichen, OK setzt es. „Fertig“ übernimmt die Eingabe, Zurück verwirft sie.")
            color: Theme.textFaint; font.pixelSize: 22 * kb.u; wrapMode: Text.WordWrap
        }
    }
}
