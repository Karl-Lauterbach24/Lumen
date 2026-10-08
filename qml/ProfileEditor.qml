import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen.Core

// Editor für Ausgabeprofile (Zielgerät + optimale Einstellungen)
Popup {
    id: editor
    property var draft: ({})
    property string extraText: ""

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(820, parent ? parent.width - 40 : 820)
    height: Math.min(720, parent ? parent.height - 40 : 720)
    padding: 0
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: "#b0000000" }
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.line }

    function openFor(profile) {
        draft = JSON.parse(JSON.stringify(profile))
        const extra = draft.extra || {}
        extraText = Object.keys(extra).map(k => k + "=" + extra[k]).join("\n")
        open()
    }
    function set(key, value) {
        const d = Object.assign({}, draft)
        d[key] = value
        draft = d
    }
    function hasCodec(c) { return (draft.audioPassthrough || []).indexOf(c) >= 0 }
    function toggleCodec(c, on) {
        let list = (draft.audioPassthrough || []).filter(x => x !== c)
        if (on) list.push(c)
        set("audioPassthrough", list)
    }
    function collect() {
        const d = Object.assign({}, draft)
        const extra = {}
        extraText.split("\n").forEach(line => {
            const i = line.indexOf("=")
            if (i > 0) extra[line.slice(0, i).trim()] = line.slice(i + 1).trim()
        })
        d.extra = extra
        return d
    }

    readonly property var outputModel: [{ id: "", label: qsTr("Automatisch (höchstauflösender Bildschirm)") }].concat(Displays.outputs)
    readonly property var audioDeviceModel: Player.audioDevices.length
        ? Player.audioDevices.map(d => ({ id: d.name, label: d.description || d.name }))
        : [{ id: "auto", label: qsTr("Automatisch") }]

    component Field: ColumnLayout {
        property string title
        default property alias content: holder.data
        spacing: 5
        Layout.fillWidth: true
        Text { text: parent.title; color: Theme.textDim; font.pixelSize: 12 }
        Item { id: holder; Layout.fillWidth: true; implicitHeight: childrenRect.height }
    }

    component EnumSelect: Select {
        property string key
        property var options: []
        width: parent ? parent.width : 200
        model: options
        textRole: "text"
        valueRole: "value"
        currentIndex: indexFor(editor.draft[key])
        onActivated: editor.set(key, currentValue)
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Kopf
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 12
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                SectionLabel { text: editor.draft.builtin ? qsTr("Vorlage") : qsTr("Eigenes Profil") }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    text: editor.draft.name || ""
                    onTextEdited: editor.set("name", text)
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    leftPadding: 0
                    background: Item {}
                    selectByMouse: true
                }
            }
            IconButton { iconName: "close"; tip: qsTr("Schließen"); onClicked: editor.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            GridLayout {
                width: parent.width - 40
                x: 20
                y: 16
                columns: 2
                columnSpacing: 28
                rowSpacing: 14

                // ---------------- Gerät ----------------
                SectionLabel { text: qsTr("Ausgabegerät"); Layout.columnSpan: 2 }

                Field {
                    title: qsTr("Bildschirm / Projektor")
                    Select {
                        width: parent.width
                        model: editor.outputModel
                        textRole: "label"
                        valueRole: "id"
                        currentIndex: Math.max(0, indexFor(editor.draft.output || ""))
                        onActivated: editor.set("output", currentValue)
                    }
                }
                Field {
                    title: qsTr("Beschreibung")
                    TextField {
                        width: parent.width
                        text: editor.draft.description || ""
                        onTextEdited: editor.set("description", text)
                        color: Theme.text
                        placeholderText: qsTr("z. B. Wohnzimmer-Beamer, HDMI 2")
                        placeholderTextColor: Theme.textFaint
                        background: Rectangle { radius: Theme.radiusSmall; color: Theme.raised; border.color: Theme.line }
                    }
                }

                Toggle { label: qsTr("Vollbild"); hint: qsTr("Player-Fenster fest auf diesem Gerät"); checked: !!editor.draft.fullscreen; onToggled: editor.set("fullscreen", checked); Layout.fillWidth: true }
                Toggle { label: qsTr("Bildrate anpassen"); hint: Displays.canSwitchRefresh ? qsTr("z. B. 23.976 fps → 23/24 Hz") : qsTr("auf dieser Plattform noch nicht verfügbar"); enabled: Displays.canSwitchRefresh; checked: !!editor.draft.matchRefreshRate; onToggled: editor.set("matchRefreshRate", checked); Layout.fillWidth: true }
                Toggle { label: qsTr("Fensterrahmen"); checked: editor.draft.border !== false; onToggled: editor.set("border", checked); Layout.fillWidth: true }
                Toggle { label: qsTr("System-HDR automatisch schalten"); hint: Displays.canSwitchHdr ? qsTr("HDR an bei HDR-Inhalt, sonst aus") : qsTr("wird vom System/Compositor gesteuert"); enabled: Displays.canSwitchHdr; checked: !!editor.draft.osHdrSwitch; onToggled: editor.set("osHdrSwitch", checked); Layout.fillWidth: true }
                Toggle { label: qsTr("Immer im Vordergrund"); checked: !!editor.draft.ontop; onToggled: editor.set("ontop", checked); Layout.fillWidth: true }
                Field {
                    title: qsTr("Player-Fenster")
                    EnumSelect {
                        key: "playerWindow"
                        enabled: !Player.embeddedOnly() // macOS, Wayland: immer eingebettet
                        options: [
                            { value: "auto", text: Qt.platform.os === "osx" ? qsTr("Automatisch (macOS: eingebettet)")
                                                 : Player.embeddedOnly() ? qsTr("Automatisch (Wayland: eingebettet)") : qsTr("Automatisch (nativ)") },
                            { value: "native", text: qsTr("Nativ – HDR-Ausgabe, beste Qualität") },
                            { value: "embedded", text: qsTr("Eingebettet – Qt-Fenster, nur SDR") }
                        ]
                    }
                }

                // ---------------- Bild ----------------
                SectionLabel { text: qsTr("Bild & HDR"); Layout.columnSpan: 2 }

                Field {
                    title: qsTr("Dynamikumfang")
                    EnumSelect {
                        key: "hdr"
                        options: [
                            { value: "auto", text: qsTr("Automatisch") },
                            { value: "passthrough", text: qsTr("HDR durchreichen (Gerät mappt)") },
                            { value: "tonemap", text: qsTr("Im Player tone-mappen") }
                        ]
                    }
                }
                Field {
                    title: qsTr("Tonemapping-Kurve")
                    EnumSelect {
                        key: "toneMapping"
                        enabled: editor.draft.hdr === "tonemap"
                        options: [
                            { value: "auto", text: qsTr("Automatisch") },
                            { value: "bt.2390", text: qsTr("BT.2390 (Referenz)") },
                            { value: "spline", text: qsTr("Spline (weich)") },
                            { value: "bt.2446a", text: qsTr("BT.2446a") },
                            { value: "st2094-40", text: qsTr("ST 2094-40 (HDR10+)") },
                            { value: "clip", text: qsTr("Clip (keins)") }
                        ]
                    }
                }
                Field {
                    title: qsTr("Zielfarbraum")
                    EnumSelect {
                        key: "targetPrim"
                        options: [
                            { value: "auto", text: qsTr("Automatisch") },
                            { value: "bt.709", text: qsTr("BT.709 (SDR / HD)") },
                            { value: "dci-p3", text: qsTr("DCI-P3") },
                            { value: "display-p3", text: qsTr("Display P3") },
                            { value: "bt.2020", text: qsTr("BT.2020 (HDR)") }
                        ]
                    }
                }
                Field {
                    title: qsTr("Ziel-Transferkurve")
                    EnumSelect {
                        key: "targetTrc"
                        options: [
                            { value: "auto", text: qsTr("Automatisch") },
                            { value: "bt.1886", text: qsTr("BT.1886 / Gamma 2.4 (Beamer, dunkler Raum)") },
                            { value: "gamma2.6", text: qsTr("Gamma 2.6 (Digitalkino, DCI)") },
                            { value: "gamma2.4", text: qsTr("Gamma 2.4 (reine Potenzfunktion)") },
                            { value: "gamma2.2", text: qsTr("Gamma 2.2 (heller Raum)") },
                            { value: "srgb", text: "sRGB" },
                            { value: "pq", text: qsTr("PQ (HDR10-Ausgabe)") },
                            { value: "hlg", text: qsTr("HLG") }
                        ]
                    }
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Spitzenhelligkeit des Geräts (0 = automatisch)")
                    from: 0; to: 1500; stepSize: 10; unit: qsTr(" nits")
                    value: editor.draft.targetPeak || 0
                    onMoved: v => editor.set("targetPeak", v)
                }
                Field {
                    title: qsTr("Skalierungsqualität")
                    EnumSelect {
                        key: "quality"
                        options: [
                            { value: "auto", text: qsTr("Automatisch (nach Grafikhardware)") },
                            { value: "fast", text: qsTr("Schnell (geringe GPU-Last)") },
                            { value: "balanced", text: qsTr("Ausgewogen") },
                            { value: "high", text: qsTr("Hoch (EWA Lanczos, lineares Downscaling)") },
                            { value: "reference", text: qsTr("Referenz (Lanczos 4, Error Diffusion, HDR-Kontrast)") }
                        ]
                    }
                }
                Field {
                    title: qsTr("Synchronisation")
                    EnumSelect {
                        key: "videoSync"
                        options: [
                            { value: "display-resample", text: qsTr("An Anzeige (ruckelfrei, empfohlen)") },
                            { value: "audio", text: qsTr("An Audio (für Bitstream sicherer)") }
                        ]
                    }
                }
                Field {
                    title: qsTr("Hardware-Decoding")
                    EnumSelect {
                        key: "hwdec"
                        options: [
                            { value: "smart", text: qsTr("Automatisch") },
                            { value: "auto-safe", text: "auto-safe (mpv)" },
                            { value: "no", text: qsTr("Aus (Software)") },
                            { value: "d3d11va", text: qsTr("D3D11VA (Windows)") },
                            { value: "nvdec", text: qsTr("NVDEC (NVIDIA)") },
                            { value: "vaapi", text: qsTr("VA-API (Linux)") },
                            { value: "videotoolbox", text: qsTr("VideoToolbox (macOS)") }
                        ]
                    }
                }
                Toggle {
                    label: qsTr("Leistung automatisch anpassen")
                    hint: qsTr("Gehen Bilder verloren, nimmt Lumen erst die Skalierung, dann den Decoder schrittweise zurück")
                    checked: editor.draft.adaptive !== false
                    onToggled: editor.set("adaptive", checked)
                    Layout.fillWidth: true
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: !!Player.hardware.renderer
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: qsTr("Grafik: ") + (Player.hardware.renderer || "")
                }
                // ---------------- Kalibrierung ----------------
                SectionLabel { text: qsTr("Kalibrierung"); Layout.columnSpan: 2 }
                Toggle { label: qsTr("ICC-Profil des Systems"); hint: qsTr("Farbprofil des Bildschirms automatisch verwenden"); checked: !!editor.draft.iccAuto; onToggled: editor.set("iccAuto", checked); Layout.fillWidth: true }
                Field {
                    title: qsTr("Eigenes ICC-Profil (Pfad, optional)")
                    TextField {
                        width: parent.width
                        text: editor.draft.iccProfile || ""
                        onTextEdited: editor.set("iccProfile", text)
                        color: Theme.text; placeholderText: qsTr("z. B. C:/Kalibrierung/beamer.icc"); placeholderTextColor: Theme.textFaint
                        background: Rectangle { radius: Theme.radiusSmall; color: Theme.raised; border.color: Theme.line }
                    }
                }
                Field {
                    title: qsTr("3D-LUT der Anzeige (.cube, z. B. aus DisplayCAL/Calman)")
                    TextField {
                        width: parent.width
                        text: editor.draft.targetLut || ""
                        onTextEdited: editor.set("targetLut", text)
                        color: Theme.text; placeholderText: qsTr("leer = keine"); placeholderTextColor: Theme.textFaint
                        background: Rectangle { radius: Theme.radiusSmall; color: Theme.raised; border.color: Theme.line }
                    }
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Nativer Kontrast des Geräts (0 = automatisch)")
                    from: 0; to: 20000; stepSize: 100; unit: ":1"
                    value: editor.draft.targetContrast || 0
                    onMoved: v => editor.set("targetContrast", v)
                }
                Field {
                    title: qsTr("Dithering")
                    RowLayout {
                        width: parent.width
                        EnumSelect {
                            key: "dither"
                            Layout.fillWidth: true
                            options: [
                                { value: "auto", text: qsTr("Automatisch") },
                                { value: "error-diffusion", text: qsTr("Error Diffusion (beste)") },
                                { value: "ordered", text: qsTr("Geordnet") },
                                { value: "no", text: qsTr("Aus") }
                            ]
                        }
                        EnumSelect {
                            key: "ditherDepth"
                            Layout.fillWidth: true
                            options: [
                                { value: "auto", text: qsTr("Tiefe: auto") },
                                { value: "8", text: qsTr("8 Bit") },
                                { value: "10", text: qsTr("10 Bit") },
                                { value: "12", text: qsTr("12 Bit") }
                            ]
                        }
                    }
                }
                Field {
                    Layout.columnSpan: 2
                    title: qsTr("GLSL-Shader (je Zeile ein Pfad, z. B. FSRCNNX, KrigBilateral)")
                    TextArea {
                        width: parent.width
                        height: 54
                        text: editor.draft.shaders || ""
                        onTextChanged: if (text !== (editor.draft.shaders || "")) editor.set("shaders", text)
                        color: Theme.text; font.family: Theme.mono; font.pixelSize: 12
                        background: Rectangle { radius: Theme.radiusSmall; color: Theme.raised; border.color: Theme.line }
                    }
                }

                Toggle { label: qsTr("Zwischenbildberechnung"); hint: qsTr("Glättet 24p auf 60 Hz – nicht nötig bei 24-Hz-Modus"); checked: !!editor.draft.interpolation; onToggled: editor.set("interpolation", checked); Layout.fillWidth: true }
                Toggle { label: qsTr("Debanding"); hint: qsTr("Entfernt Farbabstufungen"); checked: editor.draft.deband !== false; onToggled: editor.set("deband", checked); Layout.fillWidth: true }
                Field {
                    title: qsTr("Renderer / Grafik-API")
                    RowLayout {
                        width: parent.width
                        EnumSelect {
                            key: "vo"
                            Layout.fillWidth: true
                            options: [ { value: "gpu-next", text: "gpu-next" }, { value: "gpu", text: qsTr("gpu (alt)") } ]
                        }
                        EnumSelect {
                            key: "gpuApi"
                            Layout.fillWidth: true
                            options: [
                                { value: "auto", text: qsTr("API: auto") },
                                { value: "d3d11", text: qsTr("Direct3D 11") },
                                { value: "vulkan", text: qsTr("Vulkan") },
                                { value: "opengl", text: qsTr("OpenGL") }
                            ]
                        }
                    }
                }

                // ---------------- 3D ----------------
                SectionLabel { text: "3D"; Layout.columnSpan: 2 }
                Field {
                    title: qsTr("3D-Format, das das Gerät erwartet")
                    EnumSelect {
                        key: "stereoOut"
                        options: [
                            { value: "none", text: qsTr("Kein 3D (3D-Quellen als 2D)") },
                            { value: "fp", text: qsTr("HDMI Frame Packing 1080p (1920×2205) – volle Auflösung je Auge") },
                            { value: "sbs2l", text: qsTr("Side-by-Side Half") },
                            { value: "sbsl", text: qsTr("Side-by-Side Full") },
                            { value: "ab2l", text: qsTr("Top-and-Bottom Half") },
                            { value: "abl", text: qsTr("Top-and-Bottom Full") },
                            { value: "irl", text: qsTr("Zeilenverschachtelt (passiv)") },
                            { value: "arcd", text: qsTr("Anaglyph Rot/Cyan") },
                            { value: "seq", text: qsTr("Bildfolge für Shutterbrillen (experimentell)") }
                        ]
                    }
                }

                // ---- Bildfolge (Shutterbrille): Muster und Feinabstimmung ----
                Text {
                    visible: editor.draft.stereoOut === "seq"
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    wrapMode: Text.WordWrap
                    color: Theme.warn
                    font.pixelSize: 11
                    text: qsTr("Experimentell und ohne Gewähr: Lumen zeigt je Bildwechsel abwechselnd das linke und rechte Auge. Synchronbilder (S) sind ein Versuch, DLP-Link-Brillen auf einem schnellen Fernseher zu takten – ob eine Brille darauf einrastet, hängt vom Modell ab. Das Messfeld ist für Lichtsensoren gedacht, die eine Shutterbrille steuern. Nötig sind ein Bildschirm mit 120 Hz oder mehr, abgeschaltete Bewegungsglättung und Vollbild; ein ausgelassenes Bild vertauscht die Augen.")
                }
                Field {
                    visible: editor.draft.stereoOut === "seq"
                    title: qsTr("Bildwiederholrate des Geräts")
                    EnumSelect {
                        key: "seqRate"
                        options: [96, 100, 120, 144, 165, 180, 200, 240, 288, 360].map(hz => ({ value: hz, text: hz + " Hz" }))
                    }
                }
                Field {
                    visible: editor.draft.stereoOut === "seq"
                    title: qsTr("Muster je Durchlauf (L links, R rechts, S Synchronbild, B Schwarzbild)")
                    RowLayout {
                        width: parent.width
                        EnumSelect {
                            key: "seqPattern"
                            Layout.fillWidth: true
                            options: [
                                { value: "LR", text: qsTr("L R – einfacher Wechsel") },
                                { value: "LSRS", text: qsTr("L S R S – Synchronbild nach jedem Auge") },
                                { value: "LRS", text: qsTr("L R S – ein Synchronbild je Bildpaar") },
                                { value: "LBRB", text: qsTr("L B R B – Schwarzbild nach jedem Auge") },
                                { value: "LLRR", text: qsTr("L L R R – jedes Auge zwei Bildwechsel lang") },
                                { value: "LLSRRS", text: "L L S R R S" }
                            ]
                            placeholder: qsTr("eigenes Muster")
                        }
                        TextField {
                            Layout.preferredWidth: 120
                            text: editor.draft.seqPattern || "LR"
                            maximumLength: 12
                            color: Theme.text; font.family: Theme.mono; font.pixelSize: 12
                            background: Rectangle { radius: Theme.radiusSmall; color: Theme.raised; border.color: Theme.line }
                            onEditingFinished: editor.set("seqPattern", text.toUpperCase().replace(/[^LRSB]/g, "") || "LR")
                        }
                    }
                }
                Field {
                    visible: editor.draft.stereoOut === "seq"
                    title: qsTr("Farbe der Synchronbilder")
                    EnumSelect {
                        key: "seqSyncColor"
                        options: [
                            { value: "#ff0000", text: qsTr("Rot") },
                            { value: "#ffffff", text: qsTr("Weiß") },
                            { value: "#00ff00", text: qsTr("Grün") },
                            { value: "#0000ff", text: qsTr("Blau") }
                        ]
                    }
                }
                ValueSlider {
                    visible: editor.draft.stereoOut === "seq"
                    Layout.fillWidth: true
                    label: qsTr("Helligkeit der Synchronbilder")
                    from: 5; to: 100; stepSize: 5; unit: " %"
                    value: editor.draft.seqSyncLevel === undefined ? 100 : editor.draft.seqSyncLevel
                    onMoved: v => editor.set("seqSyncLevel", v)
                }
                Field {
                    visible: editor.draft.stereoOut === "seq"
                    title: qsTr("Messfeld für Lichtsensor")
                    EnumSelect {
                        key: "seqBox"
                        options: [
                            { value: "none", text: qsTr("Aus") },
                            { value: "tl", text: qsTr("Oben links") },
                            { value: "tr", text: qsTr("Oben rechts") },
                            { value: "bl", text: qsTr("Unten links") },
                            { value: "br", text: qsTr("Unten rechts") }
                        ]
                    }
                }
                ValueSlider {
                    visible: editor.draft.stereoOut === "seq"
                    Layout.fillWidth: true
                    label: qsTr("Größe des Messfelds")
                    from: 2; to: 20; stepSize: 1; unit: " %"
                    value: editor.draft.seqBoxSize || 6
                    onMoved: v => editor.set("seqBoxSize", v)
                }
                ValueSlider {
                    visible: editor.draft.stereoOut === "seq"
                    Layout.fillWidth: true
                    label: qsTr("Muster verschieben (Bilder)")
                    from: 0; to: 11; stepSize: 1; unit: ""
                    value: editor.draft.seqPhase || 0
                    onMoved: v => editor.set("seqPhase", v)
                }
                Toggle {
                    visible: editor.draft.stereoOut === "seq"
                    label: qsTr("Augen tauschen")
                    hint: qsTr("Wenn die Tiefe verkehrt herum wirkt")
                    checked: !!editor.draft.seqSwap
                    onToggled: editor.set("seqSwap", checked)
                    Layout.fillWidth: true
                }
                Toggle {
                    visible: editor.draft.stereoOut === "seq"
                    label: qsTr("Bildschirm auf diese Rate umschalten")
                    hint: qsTr("Bei 3D-Wiedergabe; sonst muss der Bildschirm schon so eingestellt sein")
                    checked: !!editor.draft.seqSwitchMode
                    onToggled: editor.set("seqSwitchMode", checked)
                    Layout.fillWidth: true
                }
                Field {
                    visible: editor.draft.stereoOut === "seq" && !!editor.draft.seqSwitchMode
                    title: qsTr("Auflösung für die 3D-Bildfolge")
                    EnumSelect {
                        key: "seqResolution"
                        options: [
                            { value: "", text: qsTr("Unverändert") },
                            { value: "1920x1080", text: "1920 × 1080" },
                            { value: "2560x1440", text: "2560 × 1440" },
                            { value: "3840x2160", text: "3840 × 2160" }
                        ]
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: Player.mvcCapable
                          ? qsTr("Blu-ray 3D (MVC) wird mit beiden Ansichten dekodiert. Frame Packing: Beamer erkennt 3D automatisch, erfordert einen 1920×2205-Anzeigemodus (im Grafiktreiber anlegen). SBS/TAB: 3D-Modus am Beamer wählen.")
                          : qsTr("Geladenes FFmpeg kann kein MVC – Blu-ray 3D läuft in 2D. Lumen mit FFmpeg-mvc bauen (tools/build_deps.sh).")
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: qsTr("Untertitel-/Menütiefe (3D)")
                    from: -40; to: 40; stepSize: 1; unit: qsTr(" px")
                    value: editor.draft.subtitleDepth || 0
                    onMoved: v => editor.set("subtitleDepth", v)
                }

                // ---------------- Audio ----------------
                SectionLabel { text: qsTr("Audio"); Layout.columnSpan: 2 }
                Field {
                    title: qsTr("Audiogerät")
                    Select {
                        width: parent.width
                        model: editor.audioDeviceModel
                        textRole: "label"
                        valueRole: "id"
                        currentIndex: Math.max(0, indexFor(editor.draft.audioDevice || "auto"))
                        onActivated: editor.set("audioDevice", currentValue)
                    }
                }
                Field {
                    title: qsTr("Kanäle (PCM)")
                    EnumSelect {
                        key: "audioChannels"
                        options: [
                            { value: "auto-safe", text: qsTr("Automatisch") },
                            { value: "7.1,5.1,stereo", text: qsTr("7.1 / 5.1 / Stereo") },
                            { value: "5.1,stereo", text: qsTr("max. 5.1") },
                            { value: "stereo", text: qsTr("Stereo (Downmix)") }
                        ]
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { text: qsTr("Bitstream / Passthrough (HDMI zum AV-Receiver)"); color: Theme.textDim; font.pixelSize: 12 }
                    Toggle { Layout.fillWidth: true; label: qsTr("Dolby TrueHD / Atmos"); checked: editor.hasCodec("truehd"); onToggled: editor.toggleCodec("truehd", checked) }
                    Toggle { Layout.fillWidth: true; label: qsTr("DTS-HD MA / DTS:X"); checked: editor.hasCodec("dts-hd"); onToggled: editor.toggleCodec("dts-hd", checked) }
                    Toggle { Layout.fillWidth: true; label: qsTr("DTS Core"); checked: editor.hasCodec("dts"); onToggled: editor.toggleCodec("dts", checked) }
                    Toggle { Layout.fillWidth: true; label: qsTr("Dolby Digital Plus"); checked: editor.hasCodec("eac3"); onToggled: editor.toggleCodec("eac3", checked) }
                    Toggle { Layout.fillWidth: true; label: qsTr("Dolby Digital"); checked: editor.hasCodec("ac3"); onToggled: editor.toggleCodec("ac3", checked) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Toggle { Layout.fillWidth: true; label: qsTr("Exklusiver Modus"); hint: qsTr("WASAPI exklusiv – empfohlen für Bitstream"); checked: !!editor.draft.audioExclusive; onToggled: editor.set("audioExclusive", checked) }
                }

                // ---------------- Experte ----------------
                SectionLabel { text: qsTr("Experte – zusätzliche mpv-Optionen (key=value)"); Layout.columnSpan: 2 }
                TextArea {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.preferredHeight: 90
                    text: editor.extraText
                    onTextChanged: editor.extraText = text
                    color: Theme.text
                    font.family: Theme.mono
                    font.pixelSize: 12
                    placeholderText: qsTr("z. B.\nicc-profile-auto=yes\nblend-subtitles=video")
                    placeholderTextColor: Theme.textFaint
                    background: Rectangle { radius: Theme.radiusSmall; color: Theme.raised; border.color: Theme.line }
                }
                Item { Layout.columnSpan: 2; implicitHeight: 12 }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        // Fuß
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8

            Button {
                text: editor.draft.builtin ? qsTr("Vorlage zurücksetzen") : qsTr("Löschen")
                visible: !!editor.draft.id
                enabled: !editor.draft.builtin || !!editor.draft.modified
                flat: true
                palette.windowText: editor.draft.builtin ? Theme.textDim : Theme.bad
                onClicked: {
                    if (editor.draft.builtin) Profiles.resetBuiltin(editor.draft.id)
                    else Profiles.deleteProfile(editor.draft.id)
                    editor.close()
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Als neues Profil")
                flat: true
                palette.windowText: Theme.text
                onClicked: {
                    const d = editor.collect()
                    d.id = ""
                    d.builtin = false
                    Profiles.currentId = Profiles.saveProfile(d)
                    editor.close()
                }
            }
            Button {
                text: qsTr("Speichern & anwenden").replace("&", "&&") // sonst gilt das & als Tastenkürzel
                highlighted: true
                palette.highlight: Theme.accent
                palette.highlightedText: Theme.bg
                onClicked: {
                    const id = Profiles.saveProfile(editor.collect())
                    Profiles.currentId = id
                    editor.close()
                }
            }
        }
    }
}
