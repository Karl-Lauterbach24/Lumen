pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#0a0b0e"
    readonly property color panel: "#101217"
    readonly property color raised: "#171a21"
    readonly property color hover: "#1f232c"
    readonly property color line: "#22262f"
    readonly property color text: "#eceef3"
    readonly property color textDim: "#9299a8"
    readonly property color textFaint: "#5c6373"
    readonly property color accent: "#7b93ff"
    readonly property color accentSoft: "#1f7b93ff"
    readonly property color good: "#4fd18b"
    readonly property color warn: "#f2b84b"
    readonly property color bad: "#ff6376"
    readonly property color hdr: "#ffcf70"

    readonly property int radius: 12
    readonly property int radiusSmall: 7

    readonly property string font: Qt.platform.os === "windows" ? "Segoe UI Variable Text"
                                 : Qt.platform.os === "osx" ? Qt.application.font.family /* Systemschrift (SF) */ : "Inter"
    readonly property string mono: Qt.platform.os === "windows" ? "Cascadia Mono"
                                 : Qt.platform.os === "osx" ? "Menlo" : "JetBrains Mono"

    function icon(name) { return "qrc:/qt/qml/Lumen/icons/" + name + ".svg" }

    function time(s) {
        if (!(s >= 0)) return "--:--"
        s = Math.floor(s)
        const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), sec = s % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (sec < 10 ? "0" : "") + sec
    }
}
