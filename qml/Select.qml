import QtQuick
import QtQuick.Controls.Basic

// Kompakte Auswahlliste. Modelle sind JS-Arrays (Strings oder Objekte mit textRole).
ComboBox {
    id: c
    property string placeholder: ""
    property int popupWidth: 0

    implicitHeight: 34
    implicitWidth: 200
    focusPolicy: Qt.NoFocus
    font.pixelSize: 13

    // Reaktiv auf Modelländerungen (im Gegensatz zu indexOfValue())
    function indexFor(v) {
        const m = c.model
        if (!m || !m.length) return -1
        for (let i = 0; i < m.length; ++i)
            if ((c.valueRole ? m[i][c.valueRole] : m[i]) === v) return i
        return -1
    }

    function itemText(item) {
        if (item === undefined || item === null) return ""
        return c.textRole && typeof item === "object" ? String(item[c.textRole]) : String(item)
    }

    contentItem: Text {
        leftPadding: 12
        rightPadding: 28
        text: c.count > 0 && c.currentIndex >= 0 ? c.displayText : c.placeholder
        color: c.count > 0 && c.currentIndex >= 0 ? Theme.text : Theme.textFaint
        font: c.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: c.width - width - 11
        anchors.verticalCenter: c.verticalCenter
        text: "⌄"
        color: Theme.textDim
        font.pixelSize: 14
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: c.hovered ? Theme.hover : Theme.raised
        border.color: c.popup.visible ? Theme.accent : Theme.line
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    delegate: ItemDelegate {
        id: d
        width: ListView.view ? ListView.view.width : c.width
        height: 32
        highlighted: c.highlightedIndex === index
        contentItem: Text {
            text: c.itemText(modelData)
            color: c.currentIndex === index ? Theme.accent : Theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { radius: 5; color: d.highlighted ? Theme.hover : "transparent" }
    }

    popup: Popup {
        y: c.height + 4
        width: Math.max(c.width, c.popupWidth)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 380)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: c.popup.visible ? c.delegateModel : null
            currentIndex: c.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }
        background: Rectangle { color: Theme.raised; border.color: Theme.line; radius: Theme.radiusSmall }
    }
}
