import QtQuick
import Lumen.Core

// Eine Kachel der Startseite
Item {
    id: card
    property string title
    property string subtitle
    property string iconName
    property bool current: false
    property real u: 1
    // schmale Kachel der zweiten Reihe (zuletzt gespielt)
    property bool small: false
    // 0..1: Balken am unteren Rand (wie weit ein Film gesehen ist), < 0 = keiner
    property real progress: -1
    signal clicked()

    width: (small ? 380 : 400) * u
    height: (small ? 132 : 290) * u
    scale: current ? 1.06 : 1
    z: current ? 2 : 1
    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    // Schein um die gewählte Kachel
    Rectangle {
        anchors.fill: plate
        anchors.margins: -10 * card.u
        radius: plate.radius + 10 * card.u
        color: os.focusColor
        opacity: card.current ? 0.22 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
    Rectangle {
        id: plate
        anchors.fill: parent
        radius: (card.small ? 22 : 30) * card.u
        border.width: card.current ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.07)
        gradient: Gradient {
            GradientStop { position: 0; color: card.current ? "#6f86f7" : "#1b1f28" }
            GradientStop { position: 1; color: card.current ? "#3f56d4" : "#12151b" }
        }
        clip: true
        Rectangle {
            visible: card.progress >= 0
            anchors.left: parent.left; anchors.bottom: parent.bottom
            height: 6 * card.u
            width: parent.width * Math.max(0, Math.min(1, card.progress))
            color: card.current ? "#ffffff" : Theme.accent
            opacity: 0.9
        }
    }
    // große Kachel: Symbol oben, Text unten
    Item {
        visible: !card.small
        anchors.fill: parent
        anchors.margins: 34 * card.u
        Rectangle {
            width: 92 * card.u; height: width; radius: width / 2
            color: card.current ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, 0.06)
            Image {
                anchors.centerIn: parent
                source: card.iconName ? Os.icon(card.iconName) : ""
                sourceSize.width: 50 * card.u; sourceSize.height: 50 * card.u
            }
        }
        Column {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            spacing: 6 * card.u
            Text {
                width: parent.width
                text: card.title
                color: Theme.text
                font.family: Theme.font; font.pixelSize: 36 * card.u; font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: card.subtitle
                color: card.current ? Qt.rgba(1, 1, 1, 0.85) : Theme.textDim
                font.family: Theme.font; font.pixelSize: 23 * card.u
                elide: Text.ElideRight
            }
        }
    }
    // kleine Kachel: Symbol links, Text rechts
    Row {
        visible: card.small
        anchors.fill: parent
        anchors.margins: 24 * card.u
        spacing: 20 * card.u
        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: card.iconName ? Os.icon(card.iconName) : ""
            sourceSize.width: 42 * card.u; sourceSize.height: 42 * card.u
            opacity: card.current ? 1 : 0.7
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 62 * card.u
            spacing: 4 * card.u
            Text {
                width: parent.width
                text: card.title
                color: Theme.text
                font.family: Theme.font; font.pixelSize: 27 * card.u; font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: card.subtitle
                color: card.current ? Qt.rgba(1, 1, 1, 0.85) : Theme.textDim
                font.family: Theme.font; font.pixelSize: 21 * card.u
                elide: Text.ElideRight
            }
        }
    }
    MouseArea { anchors.fill: parent; onClicked: card.clicked() }
}
