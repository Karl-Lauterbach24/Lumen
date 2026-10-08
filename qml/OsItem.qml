import QtQuick
import QtQuick.Layouts

// Eine Zeile einer Liste auf dem Fernseher: groß, mit der Fernbedienung zu erreichen
Rectangle {
    id: item
    property string label
    property string detail
    property string iconName
    property bool current: false
    property bool dimmed: false
    property real u: 1
    signal clicked()

    implicitHeight: 84 * u
    radius: 14 * u
    color: current ? Theme.accent : Theme.raised
    opacity: dimmed ? 0.45 : 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28 * item.u
        anchors.rightMargin: 28 * item.u
        spacing: 20 * item.u
        Image {
            visible: item.iconName.length > 0
            source: item.iconName ? Theme.icon(item.iconName) : ""
            sourceSize.width: 36 * item.u; sourceSize.height: 36 * item.u
            opacity: item.current ? 1 : 0.8
        }
        Text {
            Layout.fillWidth: true
            text: item.label
            color: item.current ? Theme.bg : Theme.text
            font.pixelSize: 30 * item.u
            font.weight: item.current ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
        Text {
            Layout.maximumWidth: item.width * 0.5
            text: item.detail
            color: item.current ? Theme.bg : Theme.textDim
            font.pixelSize: 24 * item.u
            elide: Text.ElideLeft
        }
    }
    MouseArea { anchors.fill: parent; onClicked: item.clicked() }
}
