import QtQuick
import QtQuick.Controls.Basic
import Lumen.Core

// Startseite: große Kacheln für das, was man hier tut, darunter das zuletzt Gespielte.
FocusScope {
    id: home
    readonly property real u: os.u
    property int row: 0   // 0 = Kacheln, 1 = zuletzt gespielt
    readonly property bool shown: StackView.status === StackView.Active

    readonly property var cards: {
        const list = []
        if (os.disc)
            list.push({ title: os.discName, subtitle: qsTr("%1 abspielen").arg(os.disc.kindLabel), icon: "play", run: () => os.playDisc() })
        list.push({ title: qsTr("Disc"), subtitle: os.disc ? qsTr("Titel, Kopieren, Auswerfen") : qsTr("keine eingelegt"), icon: "disc", run: () => os.openPage("disc") })
        list.push({ title: qsTr("Mediathek"), subtitle: Os.places.map(p => p.name || qsTr("Interner Speicher")).join("  ·  "), icon: "folder", run: () => os.openPage("library") })
        list.push({ title: qsTr("Einstellungen"), subtitle: Os.info.addresses.length > 0 ? Os.info.addresses[0] : qsTr("kein Netz"), icon: "tune", run: () => os.openPage("settings") })
        list.push({ title: qsTr("Ausschalten"), subtitle: "", icon: "power", run: () => os.openPage("power") })
        return list
    }
    // (die Liste zeigt nur, was noch da ist: neu nachsehen, wenn ein Datenträger kommt oder geht)
    // (… und eine Disc nur, solange sie im Laufwerk liegt)
    readonly property var recents: {
        Os.places
        Drives.drives
        const away = path => (path.startsWith("/dev/") || path.startsWith("/media/disc-")) && !os.isDiscPath(path)
        return Recent.items.filter(r => !away(r.path)).slice(0, 8)
    }
    // Eine Meldung des Players steht eine Weile über den Kacheln, dann wieder die Begrüßung
    property string trouble: ""
    Connections {
        target: Player
        function onLastErrorChanged() {
            home.trouble = Player.lastError
            if (Player.lastError) troubleTimer.restart()
        }
    }
    Timer { id: troubleTimer; interval: 20000; onTriggered: home.trouble = "" }
    onRecentsChanged: if (recents.length === 0) row = 0

    // Die gewählte Kachel gehört der Seite, nicht der Liste: die Kacheln werden neu aufgebaut, sobald sich
    // an ihrem Stand etwas ändert (Netz, Datenträger, Disc), und die Liste finge dann wieder vorn an.
    property int card: 0
    function selectCard(i) {
        const before = card
        if (cards.length > 0)
            card = Math.max(0, Math.min(i, cards.length - 1))
        top.currentIndex = card
        if (card !== before && shown && !entering) os.sound("move")
    }
    // beim ersten Erscheinen kommen die Kacheln eine nach der anderen (nach dem Auftakt, siehe OsMain)
    property bool entering: true
    Timer { interval: os.introDone ? 700 : 2600; running: true; onTriggered: home.entering = false }
    onCardsChanged: Qt.callLater(() => selectCard(card))
    // eine eingelegte Disc bringt ihre Kachel nach vorn: dort steht dann auch die Auswahl
    Connections {
        target: os
        function onDiscChanged() { if (os.disc) home.selectCard(0) }
    }

    function activate() {
        os.sound("select")
        if (row === 0) { const c = cards[card]; if (c) c.run() }
        else { const r = recents[bottom.currentIndex]; if (r) os.openPath(r.path) }
    }
    Keys.onPressed: event => {
        event.accepted = true
        if (event.key === Qt.Key_Left) { if (row === 0) selectCard(card - 1); else { const i = bottom.currentIndex; bottom.decrementCurrentIndex(); if (bottom.currentIndex !== i) os.sound("move") } }
        else if (event.key === Qt.Key_Right) { if (row === 0) selectCard(card + 1); else { const i = bottom.currentIndex; bottom.incrementCurrentIndex(); if (bottom.currentIndex !== i) os.sound("move") } }
        else if (event.key === Qt.Key_Down && recents.length > 0) { if (row !== 1) os.sound("move"); row = 1 }
        else if (event.key === Qt.Key_Up) { if (row !== 0) os.sound("move"); row = 0 }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Select || event.key === Qt.Key_Space) activate()
        else event.accepted = false
    }

    Column {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 210 * home.u
        spacing: 40 * home.u

        Text {
            x: 96 * home.u
            width: parent.width - 192 * home.u
            text: home.trouble ? home.trouble
                : os.disc ? qsTr("%1 liegt im Laufwerk.").arg(os.discName)
                : qsTr("Was möchtest du sehen?")
            color: home.trouble ? Theme.bad : Theme.text
            font.family: Theme.font; font.pixelSize: (home.trouble ? 30 : 58) * home.u; font.weight: Font.DemiBold
            elide: Text.ElideRight
            // ein neuer Satz blendet herein
            onTextChanged: headlineIn.restart()
            NumberAnimation on opacity { id: headlineIn; from: 0; to: 1; duration: 420; easing.type: Easing.OutCubic }
        }
        ListView {
            id: top
            width: parent.width
            height: 330 * home.u
            orientation: ListView.Horizontal
            spacing: 30 * home.u
            leftMargin: 96 * home.u; rightMargin: 96 * home.u
            model: home.cards.length
            clip: false
            highlightMoveDuration: 160
            preferredHighlightBegin: 96 * home.u
            preferredHighlightEnd: width - 96 * home.u
            highlightRangeMode: ListView.ApplyRange
            keyNavigationEnabled: false
            onCountChanged: Qt.callLater(() => home.selectCard(home.card))
            delegate: OsCard {
                required property int index
                readonly property var entry: home.cards[index] || ({})
                y: 20 * home.u
                u: home.u
                title: entry.title || ""
                subtitle: entry.subtitle || ""
                iconName: entry.icon || ""
                current: home.row === 0 && home.card === index && home.shown
                onClicked: { home.row = 0; home.selectCard(index); home.activate() }
                appear: 0
                NumberAnimation on appear { id: cardEnter; running: false; to: 1; duration: 460; easing.type: Easing.OutCubic }
                Timer { id: cardDelay; interval: (os.introDone ? 60 : 1850) + index * 70; onTriggered: cardEnter.start() }
                Component.onCompleted: { if (home.entering) cardDelay.start(); else appear = 1 }
            }
        }
        Text {
            visible: home.recents.length > 0
            x: 96 * home.u
            text: qsTr("Zuletzt gespielt")
            color: Theme.textDim
            font.family: Theme.font; font.pixelSize: 22 * home.u; font.weight: Font.DemiBold
            font.letterSpacing: 2.2 * home.u; font.capitalization: Font.AllUppercase
        }
        ListView {
            id: bottom
            visible: home.recents.length > 0
            width: parent.width
            height: 160 * home.u
            orientation: ListView.Horizontal
            spacing: 24 * home.u
            leftMargin: 96 * home.u; rightMargin: 96 * home.u
            model: home.recents
            highlightMoveDuration: 160
            preferredHighlightBegin: 96 * home.u
            preferredHighlightEnd: width - 96 * home.u
            highlightRangeMode: ListView.ApplyRange
            delegate: OsCard {
                required property var modelData
                required property int index
                y: 10 * home.u
                u: home.u
                small: true
                title: modelData.title
                subtitle: modelData.kind === "file" && modelData.duration > 0
                          ? Theme.time(modelData.position) + " / " + Theme.time(modelData.duration) : os.kindLabel(modelData.kind)
                iconName: modelData.kind === "file" ? "film" : "disc"
                progress: modelData.kind === "file" && modelData.duration > 0 ? modelData.position / modelData.duration : -1
                current: home.row === 1 && bottom.currentIndex === index && home.shown
                onClicked: { home.row = 1; bottom.currentIndex = index; home.activate() }
            }
        }
    }
    Text {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.leftMargin: 96 * home.u; anchors.bottomMargin: 60 * home.u
        text: qsTr("Pfeiltasten wählen, OK öffnet, Zurück geht eine Seite zurück.")
        color: Theme.textFaint
        font.family: Theme.font; font.pixelSize: 21 * home.u
    }
}
