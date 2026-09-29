import QtQuick
import QtQuick.Controls.Basic

// Liste mit Einträgen {label, detail, trailing, selected}
ListView {
    id: list
    property string emptyText: ""
    signal picked(int index, var item)

    clip: true
    spacing: 2
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Text {
        anchors.centerIn: parent
        visible: list.count === 0 && list.emptyText.length > 0
        text: list.emptyText
        color: Theme.textFaint
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        width: parent.width - 40
        wrapMode: Text.WordWrap
    }

    delegate: Rectangle {
        id: row
        required property var modelData
        required property int index
        readonly property bool sel: !!modelData.selected

        width: ListView.view.width - 8
        height: modelData.detail ? 50 : 36
        radius: Theme.radiusSmall
        color: sel ? Theme.accentSoft : hover.hovered ? Theme.raised : "transparent"

        Rectangle {
            visible: row.sel
            width: 3; radius: 1.5
            height: parent.height - 16
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: trailing.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Text {
                width: parent.width
                text: row.modelData.label
                color: row.sel ? Theme.text : Theme.text
                font.pixelSize: 13
                font.weight: row.sel ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            Text {
                visible: !!row.modelData.detail
                width: parent.width
                text: row.modelData.detail || ""
                color: Theme.textDim
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        Text {
            id: trailing
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: row.modelData.trailing || ""
            color: Theme.textDim
            font.pixelSize: 12
            font.family: Theme.mono
        }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: list.picked(row.index, row.modelData) }
    }
}
