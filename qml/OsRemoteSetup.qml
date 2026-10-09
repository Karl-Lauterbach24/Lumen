import QtQuick
import QtQuick.Layouts
import Lumen.Core

// Eine Fernbedienung einrichten: Für jede Funktion drückt der Nutzer die Taste, die sie haben soll.
// Die Tasten liest Lumen dabei direkt vom Gerät (Remotes); diese Seite zeigt nur, was gefragt ist.
FocusScope {
    id: setup
    readonly property real u: os.u
    readonly property var steps: Remotes.wizardSteps
    readonly property int step: Remotes.wizardStep
    readonly property var current: steps[step] || ({})

    function label(id) {
        return ({ up: qsTr("Hoch"), down: qsTr("Runter"), left: qsTr("Links"), right: qsTr("Rechts"), ok: qsTr("OK / Auswählen"),
                  back: qsTr("Zurück"), menu: qsTr("Menü der Disc"), playpause: qsTr("Wiedergabe / Pause"), stop: qsTr("Stopp"),
                  rewind: qsTr("Zurückspulen"), forward: qsTr("Vorspulen"), prev: qsTr("Voriges Kapitel"), next: qsTr("Nächstes Kapitel"),
                  volup: qsTr("Lauter"), voldown: qsTr("Leiser"), mute: qsTr("Ton aus"), info: qsTr("Einblendmenü / Info"),
                  audio: qsTr("Tonspur wechseln"), subtitle: qsTr("Untertitel wechseln") })[id] || id
    }

    // Die Einrichtung endet von selbst (fertig, abgebrochen, Gerät weg): Seite schließen
    Connections {
        target: Remotes
        function onWizardFinished(id, saved) { os.back() }
    }
    // Tastatur: Esc bricht ab, Leertaste lässt eine wahlfreie Funktion aus
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) Remotes.cancelWizard()
        else if (event.key === Qt.Key_Space) Remotes.skipStep()
        event.accepted = true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 96 * setup.u; anchors.rightMargin: 96 * setup.u
        anchors.topMargin: 180 * setup.u; anchors.bottomMargin: 60 * setup.u
        spacing: 20 * setup.u

        Text {
            Layout.fillWidth: true
            text: qsTr("%1 einrichten").arg(Remotes.wizardDevice)
            color: Theme.text; font.family: Theme.font; font.pixelSize: 52 * setup.u; font.weight: Font.DemiBold; elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Drücke auf dem Gerät die Taste für:")
            color: Theme.textDim; font.family: Theme.font; font.pixelSize: 30 * setup.u
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 190 * setup.u
            radius: 26 * setup.u
            gradient: Gradient {
                GradientStop { position: 0; color: "#6f86f7" }
                GradientStop { position: 1; color: "#3f56d4" }
            }
            Text {
                anchors.centerIn: parent
                text: setup.label(setup.current.id)
                color: Theme.text; font.family: Theme.font; font.pixelSize: 76 * setup.u; font.weight: Font.Bold
            }
        }
        Text {
            Layout.fillWidth: true
            text: Remotes.wizardTaken ? qsTr("Diese Taste gehört schon zu „%1“. Bitte eine andere.").arg(setup.label(Remotes.wizardTaken))
                : setup.current.optional ? qsTr("Wahlfrei: Die Taste für „Zurück“ lässt diese Funktion aus, die Taste für „OK“ schließt die Einrichtung ab.")
                : qsTr("Schritt %1 von %2").arg(setup.step + 1).arg(Remotes.requiredSteps)
            color: Remotes.wizardTaken ? Theme.warn : Theme.textDim
            font.family: Theme.font; font.pixelSize: 26 * setup.u; wrapMode: Text.WordWrap
        }
        // Übersicht: was schon zugeordnet ist
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 16 * setup.u
            spacing: 12 * setup.u
            Repeater {
                model: setup.steps
                Rectangle {
                    required property var modelData
                    required property int index
                    width: chip.implicitWidth + 36 * setup.u; height: 52 * setup.u; radius: 26 * setup.u
                    color: index === setup.step ? Theme.accentSoft : "transparent"
                    border.color: modelData.done ? Theme.good : index === setup.step ? Theme.accent : Theme.line
                    border.width: 2 * setup.u
                    opacity: modelData.skipped ? 0.4 : 1
                    Text {
                        id: chip
                        anchors.centerIn: parent
                        text: (modelData.done ? "✓ " : "") + setup.label(modelData.id)
                        color: modelData.done ? Theme.good : index === setup.step ? Theme.text : Theme.textDim
                        font.family: Theme.font; font.pixelSize: 22 * setup.u
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
        Text {
            Layout.fillWidth: true
            text: qsTr("Ohne Eingabe endet die Einrichtung nach anderthalb Minuten. Mit einer Tastatur: Esc bricht ab, die Leertaste lässt eine wahlfreie Funktion aus.")
            color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 22 * setup.u; wrapMode: Text.WordWrap
        }
    }
}
