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

    readonly property var outputModel: [{ id: "", label: "Hauptbildschirm" }].concat(Displays.outputs)
    readonly property var audioDeviceModel: Player.audioDevices.length
        ? Player.audioDevices.map(d => ({ id: d.name, label: d.description || d.name }))
        : [{ id: "auto", label: "Automatisch" }]

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
                SectionLabel { text: editor.draft.builtin ? "Vorlage" : "Eigenes Profil" }
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
            IconButton { iconName: "stop"; iconSize: 14; tip: "Schließen"; onClicked: editor.close() }
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
                SectionLabel { text: "Ausgabegerät"; Layout.columnSpan: 2 }

                Field {
                    title: "Bildschirm / Projektor"
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
                    title: "Beschreibung"
                    TextField {
                        width: parent.width
                        text: editor.draft.description || ""
                        onTextEdited: editor.set("description", text)
                        color: Theme.text
                        placeholderText: "z. B. Wohnzimmer-Beamer, HDMI 2"
                        placeholderTextColor: Theme.textFaint
                        background: Rectangle { radius: Theme.radiusSmall; color: Theme.raised; border.color: Theme.line }
                    }
                }

                Toggle { label: "Vollbild"; hint: "Player-Fenster fest auf diesem Gerät"; checked: !!editor.draft.fullscreen; onToggled: editor.set("fullscreen", checked); Layout.fillWidth: true }
                Toggle { label: "Bildrate anpassen"; hint: Displays.canSwitchRefresh ? "z. B. 23.976 fps → 23/24 Hz" : "auf dieser Plattform noch nicht verfügbar"; enabled: Displays.canSwitchRefresh; checked: !!editor.draft.matchRefreshRate; onToggled: editor.set("matchRefreshRate", checked); Layout.fillWidth: true }
                Toggle { label: "Fensterrahmen"; checked: editor.draft.border !== false; onToggled: editor.set("border", checked); Layout.fillWidth: true }
                Toggle { label: "System-HDR automatisch schalten"; hint: Displays.canSwitchHdr ? "HDR an bei HDR-Inhalt, sonst aus" : "wird vom System/Compositor gesteuert"; enabled: Displays.canSwitchHdr; checked: !!editor.draft.osHdrSwitch; onToggled: editor.set("osHdrSwitch", checked); Layout.fillWidth: true }
                Toggle { label: "Immer im Vordergrund"; checked: !!editor.draft.ontop; onToggled: editor.set("ontop", checked); Layout.fillWidth: true }
                Field {
                    title: "Player-Fenster"
                    EnumSelect {
                        key: "playerWindow"
                        options: [
                            { value: "auto", text: Qt.platform.os === "osx" ? "Automatisch (macOS: eingebettet)" : "Automatisch (nativ)" },
                            { value: "native", text: "Nativ – HDR-Ausgabe, beste Qualität" },
                            { value: "embedded", text: "Eingebettet – Qt-Fenster, nur SDR" }
                        ]
                    }
                }

                // ---------------- Bild ----------------
                SectionLabel { text: "Bild & HDR"; Layout.columnSpan: 2 }

                Field {
                    title: "Dynamikumfang"
                    EnumSelect {
                        key: "hdr"
                        options: [
                            { value: "auto", text: "Automatisch" },
                            { value: "passthrough", text: "HDR durchreichen (Gerät mappt)" },
                            { value: "tonemap", text: "Im Player tone-mappen" }
                        ]
                    }
                }
                Field {
                    title: "Tonemapping-Kurve"
                    EnumSelect {
                        key: "toneMapping"
                        enabled: editor.draft.hdr === "tonemap"
                        options: [
                            { value: "auto", text: "Automatisch" },
                            { value: "bt.2390", text: "BT.2390 (Referenz)" },
                            { value: "spline", text: "Spline (weich)" },
                            { value: "bt.2446a", text: "BT.2446a" },
                            { value: "st2094-40", text: "ST 2094-40 (HDR10+)" },
                            { value: "clip", text: "Clip (keins)" }
                        ]
                    }
                }
                Field {
                    title: "Zielfarbraum"
                    EnumSelect {
                        key: "targetPrim"
                        options: [
                            { value: "auto", text: "Automatisch" },
                            { value: "bt.709", text: "BT.709 (SDR / HD)" },
                            { value: "dci-p3", text: "DCI-P3" },
                            { value: "display-p3", text: "Display P3" },
                            { value: "bt.2020", text: "BT.2020 (HDR)" }
                        ]
                    }
                }
                Field {
                    title: "Ziel-Transferkurve"
                    EnumSelect {
                        key: "targetTrc"
                        options: [
                            { value: "auto", text: "Automatisch" },
                            { value: "bt.1886", text: "BT.1886 / Gamma 2.4 (Beamer, dunkler Raum)" },
                            { value: "gamma2.2", text: "Gamma 2.2 (heller Raum)" },
                            { value: "srgb", text: "sRGB" },
                            { value: "pq", text: "PQ (HDR10-Ausgabe)" },
                            { value: "hlg", text: "HLG" }
                        ]
                    }
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: "Spitzenhelligkeit des Geräts (0 = automatisch)"
                    from: 0; to: 1500; stepSize: 10; unit: " nits"
                    value: editor.draft.targetPeak || 0
                    onMoved: v => editor.set("targetPeak", v)
                }
                Field {
                    title: "Skalierungsqualität"
                    EnumSelect {
                        key: "quality"
                        options: [
                            { value: "fast", text: "Schnell (geringe GPU-Last)" },
                            { value: "balanced", text: "Ausgewogen" },
                            { value: "high", text: "Hoch (EWA Lanczos, lineares Downscaling)" }
                        ]
                    }
                }
                Field {
                    title: "Synchronisation"
                    EnumSelect {
                        key: "videoSync"
                        options: [
                            { value: "display-resample", text: "An Anzeige (ruckelfrei, empfohlen)" },
                            { value: "audio", text: "An Audio (für Bitstream sicherer)" }
                        ]
                    }
                }
                Field {
                    title: "Hardware-Decoding"
                    EnumSelect {
                        key: "hwdec"
                        options: [
                            { value: "auto-safe", text: "Automatisch" },
                            { value: "no", text: "Aus (Software)" },
                            { value: "d3d11va", text: "D3D11VA (Windows)" },
                            { value: "nvdec", text: "NVDEC (NVIDIA)" },
                            { value: "vaapi", text: "VA-API (Linux)" },
                            { value: "videotoolbox", text: "VideoToolbox (macOS)" }
                        ]
                    }
                }
                Toggle { label: "Zwischenbildberechnung"; hint: "Glättet 24p auf 60 Hz – nicht nötig bei 24-Hz-Modus"; checked: !!editor.draft.interpolation; onToggled: editor.set("interpolation", checked); Layout.fillWidth: true }
                Toggle { label: "Debanding"; hint: "Entfernt Farbabstufungen"; checked: editor.draft.deband !== false; onToggled: editor.set("deband", checked); Layout.fillWidth: true }
                Field {
                    title: "Renderer / Grafik-API"
                    RowLayout {
                        width: parent.width
                        EnumSelect {
                            key: "vo"
                            Layout.fillWidth: true
                            options: [ { value: "gpu-next", text: "gpu-next" }, { value: "gpu", text: "gpu (alt)" } ]
                        }
                        EnumSelect {
                            key: "gpuApi"
                            Layout.fillWidth: true
                            options: [
                                { value: "auto", text: "API: auto" },
                                { value: "d3d11", text: "Direct3D 11" },
                                { value: "vulkan", text: "Vulkan" },
                                { value: "opengl", text: "OpenGL" }
                            ]
                        }
                    }
                }

                // ---------------- 3D ----------------
                SectionLabel { text: "3D"; Layout.columnSpan: 2 }
                Field {
                    title: "3D-Format, das das Gerät erwartet"
                    EnumSelect {
                        key: "stereoOut"
                        options: [
                            { value: "none", text: "Kein 3D (3D-Quellen als 2D)" },
                            { value: "fp", text: "HDMI Frame Packing 1080p (1920×2205) – volle Auflösung je Auge" },
                            { value: "sbs2l", text: "Side-by-Side Half" },
                            { value: "sbsl", text: "Side-by-Side Full" },
                            { value: "ab2l", text: "Top-and-Bottom Half" },
                            { value: "abl", text: "Top-and-Bottom Full" },
                            { value: "irl", text: "Zeilenverschachtelt (passiv)" },
                            { value: "arcd", text: "Anaglyph Rot/Cyan" }
                        ]
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: Player.mvcCapable
                          ? "Blu-ray 3D (MVC) wird mit beiden Ansichten dekodiert. Frame Packing: Beamer erkennt 3D automatisch, erfordert einen 1920×2205-Anzeigemodus (im Grafiktreiber anlegen). SBS/TAB: 3D-Modus am Beamer wählen."
                          : "Geladenes FFmpeg kann kein MVC – Blu-ray 3D läuft in 2D. Lumen mit FFmpeg-mvc bauen (tools/build_ffmpeg_mvc.sh)."
                }
                ValueSlider {
                    Layout.fillWidth: true
                    label: "Untertitel-/Menütiefe (3D)"
                    from: -40; to: 40; stepSize: 1; unit: " px"
                    value: editor.draft.subtitleDepth || 0
                    onMoved: v => editor.set("subtitleDepth", v)
                }

                // ---------------- Audio ----------------
                SectionLabel { text: "Audio"; Layout.columnSpan: 2 }
                Field {
                    title: "Audiogerät"
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
                    title: "Kanäle (PCM)"
                    EnumSelect {
                        key: "audioChannels"
                        options: [
                            { value: "auto-safe", text: "Automatisch" },
                            { value: "7.1,5.1,stereo", text: "7.1 / 5.1 / Stereo" },
                            { value: "5.1,stereo", text: "max. 5.1" },
                            { value: "stereo", text: "Stereo (Downmix)" }
                        ]
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { text: "Bitstream / Passthrough (HDMI zum AV-Receiver)"; color: Theme.textDim; font.pixelSize: 12 }
                    Toggle { Layout.fillWidth: true; label: "Dolby TrueHD / Atmos"; checked: editor.hasCodec("truehd"); onToggled: editor.toggleCodec("truehd", checked) }
                    Toggle { Layout.fillWidth: true; label: "DTS-HD MA / DTS:X"; checked: editor.hasCodec("dts-hd"); onToggled: editor.toggleCodec("dts-hd", checked) }
                    Toggle { Layout.fillWidth: true; label: "DTS Core"; checked: editor.hasCodec("dts"); onToggled: editor.toggleCodec("dts", checked) }
                    Toggle { Layout.fillWidth: true; label: "Dolby Digital Plus"; checked: editor.hasCodec("eac3"); onToggled: editor.toggleCodec("eac3", checked) }
                    Toggle { Layout.fillWidth: true; label: "Dolby Digital"; checked: editor.hasCodec("ac3"); onToggled: editor.toggleCodec("ac3", checked) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Toggle { Layout.fillWidth: true; label: "Exklusiver Modus"; hint: "WASAPI exklusiv – empfohlen für Bitstream"; checked: !!editor.draft.audioExclusive; onToggled: editor.set("audioExclusive", checked) }
                }

                // ---------------- Experte ----------------
                SectionLabel { text: "Experte – zusätzliche mpv-Optionen (key=value)"; Layout.columnSpan: 2 }
                TextArea {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.preferredHeight: 90
                    text: editor.extraText
                    onTextChanged: editor.extraText = text
                    color: Theme.text
                    font.family: Theme.mono
                    font.pixelSize: 12
                    placeholderText: "z. B.\nicc-profile-auto=yes\nblend-subtitles=video"
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
                text: editor.draft.builtin ? "Vorlage zurücksetzen" : "Löschen"
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
                text: "Als neues Profil"
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
                text: "Speichern & anwenden"
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
