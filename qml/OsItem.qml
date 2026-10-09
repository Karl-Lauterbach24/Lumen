import QtQuick
import QtQuick.Layouts
import Lumen.Core

// Eine Zeile einer Liste auf dem Fernseher: groß, mit der Fernbedienung zu erreichen
Item {
    id: item
    property string label
    property string detail
    property string iconName
    property bool current: false
    property bool dimmed: false
    // ein Schalter: true/false zeigt ihn, undefined = kein Schalter
    property var checked: undefined
    property real u: 1
    signal clicked()

    implicitHeight: 92 * u
    opacity: dimmed ? 0.4 : 1

    Rectangle {
        id: plate
        anchors.fill: parent
        radius: 20 * item.u
        color: item.current ? os.focusColor : Qt.rgba(1, 1, 1, 0.045)
        border.width: item.current ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.06)
        scale: item.current ? 1.012 : 1
        Behavior on color { ColorAnimation { duration: 110 } }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 30 * item.u
        anchors.rightMargin: 30 * item.u
        spacing: 22 * item.u
        Image {
            visible: item.iconName.length > 0
            source: item.iconName ? Os.icon(item.iconName) : ""
            sourceSize.width: 40 * item.u; sourceSize.height: 40 * item.u
            opacity: item.current ? 1 : 0.7
        }
        Text {
            Layout.fillWidth: true
            text: item.label
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: 30 * item.u
            font.weight: item.current ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
        Text {
            visible: item.checked === undefined
            Layout.maximumWidth: item.width * 0.48
            text: item.detail
            color: item.current ? Qt.rgba(1, 1, 1, 0.86) : Theme.textDim
            font.family: Theme.font
            font.pixelSize: 24 * item.u
            elide: Text.ElideLeft
        }
        // Schalter
        Rectangle {
            visible: item.checked !== undefined
            width: 76 * item.u; height: 42 * item.u; radius: height / 2
            color: item.checked ? Theme.good : Qt.rgba(1, 1, 1, 0.16)
            Behavior on color { ColorAnimation { duration: 120 } }
            Rectangle {
                width: 32 * item.u; height: width; radius: width / 2
                y: 5 * item.u
                x: item.checked ? parent.width - width - 5 * item.u : 5 * item.u
                color: "#ffffff"
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
    }
    MouseArea { anchors.fill: parent; onClicked: item.clicked() }
}
