import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    id: t
    property string label
    property string hint

    checkable: true
    focusPolicy: Qt.NoFocus
    implicitHeight: hint ? 44 : 32
    implicitWidth: 240
    opacity: enabled ? 1 : 0.45

    contentItem: RowLayout {
        spacing: 12
        Column {
            Layout.fillWidth: true
            spacing: 2
            Text { text: t.label; color: Theme.text; font.pixelSize: 13; width: parent.width; elide: Text.ElideRight }
            Text { visible: !!t.hint; text: t.hint; color: Theme.textFaint; font.pixelSize: 11; width: parent.width; elide: Text.ElideRight }
        }
        Rectangle {
            implicitWidth: 34
            implicitHeight: 20
            radius: 10
            color: t.checked ? Theme.accent : Theme.line
            Behavior on color { ColorAnimation { duration: 120 } }
            Rectangle {
                width: 14; height: 14; radius: 7
                y: 3
                x: t.checked ? parent.width - width - 3 : 3
                color: Theme.text
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
    }
    background: null
}
