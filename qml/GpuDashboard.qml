import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Painel dedicado à GPU selecionada: sem ScrollView e sem misturar inventários globais.
Item {
    id: dashboard
    property var telemetry: null
    property var device: ({})
    property var history: []
    property string deviceKey: ""
    property color panelColor: "#ffffff"
    property color borderColor: "#d7e0e8"
    property color textColor: "#15202c"
    property color secondaryColor: "#657182"
    property color accentColor: "#ac7ddc"
    property real baseFont: 13
    property string systemFontFamily: "Sans Serif"
    readonly property bool compact: height < 580
    readonly property int tileHeight: compact ? 53 : (height < 660 ? 71 : 85)
    readonly property int statHeight: compact ? 30 : 39
    property var engineItems: [
        {label:"3D / Render", key:"renderPercent"},
        {label:"Copy", key:"copyPercent"},
        {label:"Video Encode", key:"encodePercent"},
        {label:"Video Decode", key:"decodePercent"}
    ]

    function stringOr(value, fallback) {
        return value === undefined || value === null || String(value).trim().length === 0
            ? (fallback || "—") : String(value)
    }
    function percent(value) {
        if (value === undefined || value === null || !isFinite(Number(value)) || Number(value) < 0) return "—"
        return Number(value).toFixed(1) + "%"
    }
    function temperature(value) {
        if (value === undefined || value === null || !isFinite(Number(value)) || Number(value) < -30) return "Não disponível"
        return Number(value).toFixed(1) + " °C"
    }
    function vram() {
        const total = Number(device.memoryTotalMiB)
        const used = Number(device.memoryUsedMiB)
        return isFinite(total) && isFinite(used) && total > 0 && used >= 0
               ? used.toFixed(0) + " / " + total.toFixed(0) + " MiB" : "—"
    }
    property var stats: [
        {label:"Utilização", value:percent(device.percent)},
        {label:"Temperatura", value:temperature(device.temperature)},
        {label:"VRAM dedicada", value:vram()},
        {label:"Driver", value:stringOr(device.driver)},
        {label:"Endereço PCI", value:stringOr(device.pci)},
        {label:"Link PCIe", value:stringOr(device.pcieLink)},
        {label:"Vulkan desta GPU", value:stringOr(device.vulkanVersion, "Não associado")},
        {label:"Dispositivo Vulkan", value:stringOr(device.vulkanDeviceName, "Não associado")},
        {label:"Driver Vulkan", value:stringOr(device.vulkanDriver, "Não informado")},
        {label:"Dispositivo DRM", value:stringOr(device.drmCard)},
        {label:"Vendor PCI", value:stringOr(device.vendor)},
        {label:"Device ID", value:stringOr(device.deviceId)},
        {label:"OpenGL (contexto global)", value:telemetry ? stringOr(telemetry.openGlInfo) : "—"}
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: dashboard.compact ? 3 : 6
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: dashboard.compact ? 38 : 46
            spacing: 10
            Text {
                text: dashboard.stringOr(dashboard.device.label, "GPU")
                font.family: dashboard.systemFontFamily
                font.pixelSize: Math.round(dashboard.baseFont * 1.9)
                font.weight: Font.DemiBold
                color: dashboard.textColor
            }
            Text {
                Layout.fillWidth: true
                text: dashboard.stringOr(dashboard.device.model, "Dispositivo gráfico não identificado")
                font.family: dashboard.systemFontFamily
                font.pixelSize: Math.round(dashboard.baseFont * 0.95)
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: dashboard.secondaryColor
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: dashboard.compact ? 52 : 85
            Layout.maximumHeight: dashboard.compact ? 95 : 180
            color: dashboard.panelColor
            border.color: dashboard.borderColor
            radius: 8
            HistoryGraph {
                anchors.fill: parent
                anchors.margins: 5
                samples: dashboard.history
                graphColor: dashboard.accentColor
                fillColor: Qt.rgba(dashboard.accentColor.r, dashboard.accentColor.g, dashboard.accentColor.b, 0.16)
                gridColor: dashboard.borderColor
                maxValue: 100
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: dashboard.compact ? 18 : 22
            Text {
                text: "Atividade da GPU · 60 amostras"
                color: dashboard.secondaryColor
                font.family: dashboard.systemFontFamily
                font.pixelSize: Math.round(dashboard.baseFont * 0.85)
            }
            Item { Layout.fillWidth: true }
            Text {
                text: dashboard.percent(dashboard.device.percent)
                color: dashboard.accentColor
                font.family: dashboard.systemFontFamily
                font.pixelSize: Math.round(dashboard.baseFont * 1.35)
                font.bold: true
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 7
            rowSpacing: dashboard.compact ? 3 : 6
            Repeater {
                model: dashboard.engineItems
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: dashboard.tileHeight
                    color: dashboard.panelColor
                    radius: 7
                    border.color: dashboard.borderColor
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: dashboard.compact ? 4 : 7
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: modelData.label
                                font.family: dashboard.systemFontFamily
                                font.pixelSize: Math.max(10, Math.round(dashboard.baseFont * 0.84))
                                color: dashboard.secondaryColor
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: dashboard.percent(dashboard.device[modelData.key])
                                font.family: dashboard.systemFontFamily
                                font.pixelSize: Math.max(10, Math.round(dashboard.baseFont * 0.9))
                                color: dashboard.textColor
                                font.bold: true
                            }
                        }
                        HistoryGraph {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            samples: {
                                if (!dashboard.telemetry) return []
                                const inventory = dashboard.telemetry.devices
                                return dashboard.telemetry.historyFor(dashboard.deviceKey + "#" + modelData.key)
                            }
                            graphColor: dashboard.accentColor
                            fillColor: Qt.rgba(dashboard.accentColor.r, dashboard.accentColor.g, dashboard.accentColor.b, 0.08)
                            gridColor: dashboard.borderColor
                            maxValue: 100
                            showGrid: false
                        }
                    }
                    ToolTip.visible: tip.hovered
                    ToolTip.text: modelData.label + ": " + dashboard.percent(dashboard.device[modelData.key])
                    HoverHandler { id: tip }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: dashboard.width > 650 ? 4 : 3
            columnSpacing: 6
            rowSpacing: dashboard.compact ? 2 : 5
            Repeater {
                model: dashboard.stats
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: dashboard.statHeight
                    color: dashboard.panelColor
                    border.color: dashboard.borderColor
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: modelData.label
                            font.family: dashboard.systemFontFamily
                            font.pixelSize: Math.max(9, Math.round(dashboard.baseFont * 0.73))
                            color: dashboard.secondaryColor
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            font.family: dashboard.systemFontFamily
                            font.pixelSize: Math.max(10, Math.round(dashboard.baseFont * 0.83))
                            color: dashboard.textColor
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }
                    HoverHandler { id: statHover }
                    ToolTip.visible: statHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: modelData.label + ": " + modelData.value
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: dashboard.compact ? 19 : 29
            text: "Vulkan identificado por vendor/device PCI. Engines não expostas pelo driver: —. OpenGL padrão não é atribuído a uma GPU específica."
            color: dashboard.secondaryColor
            font.family: dashboard.systemFontFamily
            font.pixelSize: Math.max(9, Math.round(dashboard.baseFont * 0.72))
            wrapMode: Text.WordWrap
            maximumLineCount: dashboard.compact ? 1 : 2
            elide: Text.ElideRight
        }
    }
}
