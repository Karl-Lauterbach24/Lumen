import QtQuick

// Zeitleiste mit Kapitelmarken, A-B-Bereich, Pufferanzeige und Vorschau-Zeit.
Item {
    id: root
    property real position: 0
    property real duration: 0
    property var chapters: []
    property real loopA: -1
    property real loopB: -1
    property real cacheSeconds: 0

    signal seekRequested(real seconds)
    signal scrub(real seconds)

    implicitHeight: 30
    readonly property bool active: mouse.containsMouse || mouse.pressed
    readonly property real frac: duration > 0 ? Math.min(1, Math.max(0, position / duration)) : 0
    property real dragFrac: 0
    readonly property real shownFrac: mouse.pressed ? dragFrac : frac

    function fracAt(x) { return Math.min(1, Math.max(0, x / width)) }
    function chapterAt(t) {
        let name = ""
        for (let i = 0; i < chapters.length; ++i)
            if (chapters[i].time <= t) name = chapters[i].title
        return name
    }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: root.active ? 6 : 4
        radius: height / 2
        color: Theme.line
        Behavior on height { NumberAnimation { duration: 100 } }

        Rectangle { // Puffer
            height: parent.height; radius: parent.radius
            color: "#2d3240"
            width: root.duration > 0 ? parent.width * Math.min(1, (root.position + root.cacheSeconds) / root.duration) : 0
        }
        Rectangle { // A-B-Schleife
            visible: root.loopA >= 0 && root.duration > 0
            height: parent.height
            color: Theme.warn
            opacity: 0.35
            x: parent.width * root.loopA / Math.max(1, root.duration)
            width: root.loopB > root.loopA ? parent.width * (root.loopB - root.loopA) / root.duration : 2
        }
        Rectangle { // Fortschritt
            height: parent.height; radius: parent.radius
            width: parent.width * root.shownFrac
            color: Theme.accent
        }
        Repeater { // Kapitelmarken
            model: root.duration > 0 ? root.chapters : []
            Rectangle {
                required property var modelData
                visible: modelData.time > 0.5
                x: track.width * modelData.time / root.duration - 1
                width: 2
                height: track.height
                color: Theme.bg
            }
        }
    }

    Rectangle { // Griff
        width: 14; height: 14; radius: 7
        color: Theme.text
        anchors.verticalCenter: parent.verticalCenter
        x: root.width * root.shownFrac - width / 2
        scale: root.active ? 1 : 0
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }

    Rectangle { // Vorschau-Zeit
        visible: mouse.containsMouse && root.duration > 0
        readonly property real t: root.fracAt(mouse.mouseX) * root.duration
        readonly property string chapter: root.chapterAt(t)
        width: label.implicitWidth + 16; height: 24; radius: 6
        color: Theme.raised; border.color: Theme.line
        y: -height - 4
        x: Math.min(root.width - width, Math.max(0, mouse.mouseX - width / 2))
        Text {
            id: label
            anchors.centerIn: parent
            text: Theme.time(parent.t) + (parent.chapter ? "  ·  " + parent.chapter : "")
            color: Theme.text; font.pixelSize: 11; font.family: Theme.mono
        }
    }

    Timer { // Scrubbing drosseln
        id: scrubTimer
        interval: 90
        onTriggered: root.scrub(root.dragFrac * root.duration)
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.duration > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled: root.duration > 0
        onPressed: e => { root.dragFrac = root.fracAt(e.x) }
        onPositionChanged: e => {
            if (!pressed) return
            root.dragFrac = root.fracAt(e.x)
            if (!scrubTimer.running) scrubTimer.start()
        }
        onReleased: { scrubTimer.stop(); root.seekRequested(root.dragFrac * root.duration) }
    }
}
