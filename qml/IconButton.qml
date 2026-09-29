import QtQuick
import QtQuick.Controls.Basic

Button {
    id: b
    property string iconName
    property int size: 36
    property int iconSize: 20
    property bool primary: false
    property bool active: false
    property string tip

    implicitWidth: size
    implicitHeight: size
    padding: 0
    focusPolicy: Qt.NoFocus
    display: AbstractButton.IconOnly
    icon.source: iconName ? Theme.icon(iconName) : ""
    icon.width: iconSize
    icon.height: iconSize
    icon.color: primary ? Theme.bg
              : active ? Theme.accent
              : enabled ? Theme.text : Theme.textFaint

    background: Rectangle {
        radius: b.primary ? width / 2 : Theme.radiusSmall
        color: b.primary ? (b.down ? "#c9ccd6" : b.hovered ? "#ffffff" : Theme.text)
             : b.down ? Theme.hover
             : b.hovered ? Theme.raised
             : "transparent"
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    ToolTip {
        visible: b.tip.length > 0 && b.hovered
        delay: 600
        text: b.tip
        contentItem: Text { text: b.tip; color: Theme.text; font.pixelSize: 12 }
        background: Rectangle { color: Theme.raised; border.color: Theme.line; radius: 5 }
    }
}
