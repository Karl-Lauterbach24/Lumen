import QtQuick

Rectangle {
    id: chip
    property string text
    property color tint: Theme.textDim
    property bool filled: false

    visible: text.length > 0
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    implicitWidth: label.implicitWidth + 16
    implicitHeight: 22
    radius: 5
    color: filled ? tint : Qt.rgba(tint.r, tint.g, tint.b, 0.10)
    border.color: Qt.rgba(tint.r, tint.g, tint.b, filled ? 0 : 0.35)

    Text {
        id: label
        anchors.centerIn: parent
        text: chip.text
        color: chip.filled ? Theme.bg : chip.tint
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0.4
    }
}
