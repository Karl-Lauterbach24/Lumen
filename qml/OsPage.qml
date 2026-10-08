import QtQuick
import QtQuick.Layouts

// Eine Seite von LumenOS: Titel, ein Hinweistext und eine Liste von Einträgen, die mit den
// Pfeiltasten gewählt und mit OK ausgelöst werden. Die meisten Seiten sind nur das.
//   entries: [{label, detail, icon, dimmed, run: function}]
FocusScope {
    id: page
    property string title
    property string note            // Text unter dem Titel
    property color noteColor: Theme.textDim
    property string footer          // Hinweis am unteren Rand
    property var entries: []
    property bool busy: false
    property alias currentIndex: list.currentIndex
    readonly property real u: os.u
    default property alias extra: extraColumn.data

    function activate(i) {
        const e = entries[i]
        if (e && e.run && !e.dimmed && !busy) e.run()
    }

    // Eine neu aufgebaute Liste (der Stand hat sich geändert) behält ihre Stelle
    property int keep: 0
    onEntriesChanged: Qt.callLater(() => { list.currentIndex = Math.min(keep, Math.max(0, entries.length - 1)) })

    // Zurück innerhalb der Seite (einen Schritt, nicht die Seite verlassen): liefert true, wenn erledigt
    property var backHandler: null

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            if (!(backHandler && backHandler())) os.back()
            event.accepted = true
        }
        else if (event.key === Qt.Key_Home) { os.home(); event.accepted = true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 80 * page.u
        anchors.topMargin: 64 * page.u
        spacing: 22 * page.u

        RowLayout {
            Layout.fillWidth: true
            spacing: 20 * page.u
            Text {
                Layout.fillWidth: true
                text: page.title
                color: Theme.text
                font.pixelSize: 52 * page.u
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                visible: page.busy
                text: qsTr("Einen Moment …")
                color: Theme.warn
                font.pixelSize: 26 * page.u
            }
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.note
            color: page.noteColor
            font.pixelSize: 26 * page.u
            wrapMode: Text.WordWrap
            lineHeight: 1.25
        }
        ColumnLayout { id: extraColumn; Layout.fillWidth: true; spacing: 16 * page.u; visible: children.length > 0 }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            clip: true
            spacing: 12 * page.u
            model: page.entries
            highlightMoveDuration: 120
            preferredHighlightBegin: height * 0.25
            preferredHighlightEnd: height * 0.75
            highlightRangeMode: ListView.ApplyRange
            onCurrentIndexChanged: page.keep = currentIndex
            delegate: OsItem {
                required property var modelData
                required property int index
                width: list.width
                u: page.u
                label: modelData.label || ""
                detail: modelData.detail || ""
                iconName: modelData.icon || ""
                dimmed: !!modelData.dimmed
                current: list.currentIndex === index && list.activeFocus
                onClicked: { list.currentIndex = index; page.activate(index) }
            }
            Keys.onReturnPressed: page.activate(currentIndex)
            Keys.onEnterPressed: page.activate(currentIndex)
            Keys.onSelectPressed: page.activate(currentIndex)
            Keys.onSpacePressed: page.activate(currentIndex)
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.footer
            color: Theme.textFaint
            font.pixelSize: 22 * page.u
            wrapMode: Text.WordWrap
        }
    }
}
