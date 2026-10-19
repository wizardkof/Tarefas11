import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tela de memória sem ScrollView: distribuição adaptativa ocupa a área disponível.
Item {
    id: dashboard
    property var telemetry: ({})
    property color panelColor: "#ffffff"
    property color borderColor: "#d7e0e8"
    property color textColor: "#15202c"
    property color secondaryColor: "#657182"
    property color accentColor: "#0078d4"
    property real baseFont: 13
    signal requestHardware()

    function gib(value) { return Number(value || 0).toFixed(2) + " GiB" }
    function percentage(value, total) { return Math.max(0, Math.min(1, Number(value)/Math.max(0.001, Number(total)))) }
    property real usedFraction: percentage(telemetry.memoryUsedGiB, telemetry.memoryTotalGiB)
    property real cachedFraction: Math.max(0, Math.min(1 - usedFraction,
                                              (telemetry.memoryAvailableGiB - telemetry.memoryFreeGiB) /
                                              Math.max(0.001, telemetry.memoryTotalGiB)))
    property real swapFraction: percentage(telemetry.swapUsedGiB, telemetry.swapTotalGiB)
    property var stats: [
        {name:"Instalada (DMI/udev)", value:telemetry.memoryInstalledGiB > 0 ? gib(telemetry.memoryInstalledGiB) : "Não detectada"},
        {name:"Utilizável pelo Linux", value:gib(telemetry.memoryTotalGiB)},
        {name:"Em uso (estimado)", value:gib(telemetry.memoryUsedGiB)},
        {name:"Disponível", value:gib(telemetry.memoryAvailableGiB)},
        {name:"Livre", value:gib(telemetry.memoryFreeGiB)},
        {name:"Cache de arquivos", value:gib(telemetry.memoryCachedGiB)},
        {name:"Buffers", value:gib(telemetry.memoryBuffersGiB)},
        {name:"Slab do kernel", value:gib(telemetry.memorySlabGiB)},
        {name:"Compartilhada", value:gib(telemetry.memorySharedGiB)},
        {name:"Ativa", value:gib(telemetry.memoryActiveGiB)},
        {name:"Comprometida", value:gib(telemetry.memoryCommittedGiB)},
        {name:"Swap em uso / total", value:gib(telemetry.swapUsedGiB) + " / " + gib(telemetry.swapTotalGiB)},
        {name:"ZRAM original", value:gib(telemetry.zramOriginalGiB)},
        {name:"ZRAM comprimida", value:gib(telemetry.zramCompressedGiB)},
        {name:"ZRAM custo físico", value:gib(telemetry.zramPhysicalGiB)},
        {name:"Tipo da RAM", value:telemetry.memoryType},
        {name:"Temperatura da RAM (SPD)", value:telemetry.memoryTemperature},
        {name:"Velocidade da RAM", value:telemetry.memorySpeed},
        {name:"Módulos", value:telemetry.memoryModules},
        {name:"Slots de RAM ocupados", value:telemetry.memorySlots}
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: dashboard.height < 580 ? 4 : 8
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Text { font.family: systemFontFamily; text: "Memória"; font.pixelSize: Math.round(dashboard.baseFont*2.0); font.weight: Font.DemiBold; color: dashboard.textColor }
            Item { Layout.fillWidth: true }
            Text { font.family: systemFontFamily;
                text: dashboard.gib(dashboard.telemetry.memoryTotalGiB) + " utilizáveis"
                font.pixelSize: Math.round(dashboard.baseFont*1.02)
                color: dashboard.secondaryColor
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 90
            Layout.maximumHeight: 215
            radius: 9
            color: dashboard.panelColor
            border.color: dashboard.borderColor
            HistoryGraph {
                anchors.fill: parent
                anchors.margins: 5
                samples: dashboard.telemetry.memoryHistory
                graphColor: dashboard.accentColor
                fillColor: Qt.rgba(dashboard.accentColor.r, dashboard.accentColor.g, dashboard.accentColor.b, 0.16)
                gridColor: dashboard.borderColor
                maxValue: 100
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { font.family: systemFontFamily; text: "Composição da memória física"; color: dashboard.textColor; font.pixelSize: dashboard.baseFont; font.bold: true }
            Item { Layout.fillWidth: true }
            Text { font.family: systemFontFamily; text: Number(dashboard.telemetry.memoryPercent).toFixed(1) + "% em uso"; color: dashboard.accentColor; font.pixelSize: dashboard.baseFont }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: dashboard.height < 580 ? 16 : 23
            radius: 5
            clip: true
            color: dashboard.panelColor
            border.color: dashboard.borderColor
            Row {
                anchors.fill: parent
                Rectangle { height: parent.height; width: parent.width*dashboard.usedFraction; color: dashboard.accentColor }
                Rectangle { height: parent.height; width: parent.width*dashboard.cachedFraction; color: Qt.lighter(dashboard.accentColor, 1.6) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { font.family: systemFontFamily; text: "Swap / ZRAM"; color: dashboard.textColor; font.pixelSize: dashboard.baseFont; font.bold: true }
            Item { Layout.fillWidth: true }
            Text { font.family: systemFontFamily;
                text: dashboard.gib(dashboard.telemetry.swapUsedGiB) + " / " + dashboard.gib(dashboard.telemetry.swapTotalGiB)
                color: dashboard.secondaryColor
                font.pixelSize: dashboard.baseFont
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: dashboard.height < 580 ? 13 : 18
            radius: 5
            clip: true
            color: dashboard.panelColor
            border.color: dashboard.borderColor
            Rectangle {
                height: parent.height
                width: parent.width * dashboard.swapFraction
                radius: 4
                color: "#927ad0"
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: dashboard.width > 850 ? 5 : dashboard.width > 580 ? 4 : 3
            columnSpacing: 7
            rowSpacing: dashboard.height < 580 ? 2 : 6
            Repeater {
                model: dashboard.stats
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: dashboard.height < 580 ? 37 : 47
                    radius: 7
                    color: dashboard.panelColor
                    border.color: dashboard.borderColor
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 5
                        anchors.topMargin: 3
                        anchors.bottomMargin: 3
                        spacing: 1
                        Text { font.family: systemFontFamily;
                            Layout.fillWidth: true
                            text: modelData.name
                            color: dashboard.secondaryColor
                            font.pixelSize: Math.max(9, Math.round(dashboard.baseFont*0.78))
                            elide: Text.ElideRight
                        }
                        Text { font.family: systemFontFamily;
                            Layout.fillWidth: true
                            text: modelData.value
                            color: dashboard.textColor
                            font.pixelSize: Math.max(10, Math.round(dashboard.baseFont*0.9))
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }
                    HoverHandler { id: infoHover }
                    ToolTip.visible: infoHover.hovered
                    ToolTip.text: modelData.name + ": " + modelData.value
                    ToolTip.delay: 500
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 31
            Button {
                visible: Number(dashboard.telemetry.memoryInstalledGiB) <= 0
                text: "Consultar SMBIOS (opcional / Polkit)"
                onClicked: dashboard.requestHardware()
            }
            Item { Layout.fillWidth: true }
            Text { font.family: systemFontFamily;
                text: "RAM: udev sem root · temperatura requer sensor DDR5/SPD · ZRAM não é RAM física"
                color: dashboard.secondaryColor
                font.pixelSize: Math.max(9, Math.round(dashboard.baseFont * 0.8))
                elide: Text.ElideRight
            }
        }
    }
}
