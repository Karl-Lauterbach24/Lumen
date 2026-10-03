import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Beschrifteter Regler. Doppelklick auf den Wert setzt auf defaultValue zurück.
ColumnLayout {
    id: root
    property string label
    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property real defaultValue: 0
    property int decimals: 0
    property string unit: ""
    signal moved(real value)

    spacing: 2

    RowLayout {
        Layout.fillWidth: true
        Text { text: root.label; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
        Text {
            text: (root.value > 0 && root.from < 0 ? "+" : "") + root.value.toFixed(root.decimals) + root.unit
            color: root.value !== root.defaultValue ? Theme.text : Theme.textFaint
            font.pixelSize: 12
            font.family: Theme.mono
            TapHandler { onDoubleTapped: root.moved(root.defaultValue) }
        }
    }

    Slider {
        id: s
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.stepSize
        focusPolicy: Qt.NoFocus
        implicitHeight: 22
        Accessible.name: root.label
        onMoved: root.moved(value)

        Binding on value { value: root.value; when: !s.pressed }

        background: Rectangle {
            x: s.leftPadding
            y: s.topPadding + s.availableHeight / 2 - height / 2
            width: s.availableWidth
            height: 4
            radius: 2
            color: Theme.line
            Rectangle {
                // Füllung ab dem Nullpunkt (bipolar) bzw. ab links
                readonly property real zero: Math.max(0, Math.min(1, (0 - s.from) / (s.to - s.from)))
                x: parent.width * Math.min(zero, s.visualPosition)
                width: parent.width * Math.abs(s.visualPosition - zero)
                height: parent.height
                radius: 2
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
            y: s.topPadding + s.availableHeight / 2 - height / 2
            width: 14; height: 14; radius: 7
            color: s.pressed ? "#ffffff" : Theme.text
        }
    }
}
