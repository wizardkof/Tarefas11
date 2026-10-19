import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property real baseFont: systemFontSize
    property var info: ({})
    property color panelColor: "#232831"
    property color borderColor: "#39414c"
    property color textColor: "#ffffff"
    property color secondaryColor: "#a5b1be"
    property color accentColor: "#69bdf0"
    property color backgroundColor: "#161a20"
    implicitHeight: 230
    radius: 8
    color: panelColor
    border.color: borderColor
    function value(key, fallback) {
        const v = root.info[key]
        return v === undefined || v === null || v === "" ? (fallback || "—") : String(v)
    }
    function memory(key) {
        const raw = root.info[key]
        if (raw === undefined || raw === null || raw === "" || !isFinite(Number(raw))) return "—"
        return processes.formatMemoryMiB(Number(raw))
    }
    function number(key, digits) {
        const raw = root.info[key]
        if (raw === undefined || raw === null || raw === "") return "—"
        const n = Number(raw); return isNaN(n) ? "—" : n.toFixed(digits)
    }
    ScrollView {
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Image {
                    source: root.info && root.info.iconUrl ? String(root.info.iconUrl) : ""
                    sourceSize: Qt.size(64, 64)
                    Layout.preferredWidth: 35
                    Layout.preferredHeight: 35
                    fillMode: Image.PreserveAspectFit
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { font.family: systemFontFamily; text: root.value("displayName"); color: root.textColor; font.pixelSize: Math.round(root.baseFont*(16/13)); font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { font.family: systemFontFamily; text: root.value("platform") + " · " + root.value("iconSource"); color: root.secondaryColor; font.pixelSize: Math.round(root.baseFont*(11/13)); elide: Text.ElideRight; Layout.fillWidth: true }
                }
                Text { font.family: systemFontFamily; text: "PID " + root.value("pid"); color: root.accentColor; font.pixelSize: Math.round(root.baseFont*(13/13)); font.bold: true }
            }
            Text {
                Layout.fillWidth: true
                visible: Number(root.info.groupProcessCount || 1) > 1
                text: "Grupo (" + root.info.groupProcessCount + " processos): RAM residente " +
                      root.memory("groupMemoryMiB") + "; SWAP privada " + root.memory("groupSwapMiB") +
                      ". RSS somada pode contar páginas compartilhadas mais de uma vez. Detalhes mostra apenas este PID."
                wrapMode: Text.WordWrap
                color: root.secondaryColor
                font.pixelSize: Math.round(root.baseFont * (10 / 13))
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 12
                rowSpacing: 5
                Repeater {
                    model: [
                        {k:"ppid", label:"PID pai"},
                        {k:"user", label:"Usuário"},
                        {k:"state", label:"Estado"},
                        {k:"nice", label:"Prioridade (nice)"},
                        {k:"threads", label:"Threads"},
                        {k:"cpu", label:"CPU (%)", digits:1},
                        {k:"memoryMiB", label:"RAM residente (RSS)", memory:true},
                        {k:"swapMiB", label:"SWAP privada (VmSwap)", memory:true},
                        {k:"virtualMiB", label:"Espaço virtual", memory:true},
                        {k:"sharedMiB", label:"RSS arquivos/shmem", memory:true},
                        {k:"cpuTimeSeconds", label:"Tempo de CPU (s)", digits:0},
                        {k:"readMiBs", label:"Leitura (MiB/s)", digits:2},
                        {k:"writeMiBs", label:"Gravação (MiB/s)", digits:2},
                        {k:"ioSharePercent", label:"Participação E/S (%)", digits:1},
                        {k:"networkMiBs", label:"Rede TCP (MiB/s)", digits:3},
                        {k:"networkRxMiBs", label:"TCP recebendo (MiB/s)", digits:3},
                        {k:"networkTxMiBs", label:"TCP enviando (MiB/s)", digits:3}
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 1
                        Text { font.family: systemFontFamily; text: modelData.label; font.pixelSize: Math.round(root.baseFont*(10/13)); color: root.secondaryColor; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { font.family: systemFontFamily; text: modelData.memory ? root.memory(modelData.k) : (modelData.digits === undefined ? root.value(modelData.k) : root.number(modelData.k, modelData.digits)); font.pixelSize: Math.round(root.baseFont*(12/13)); color: root.textColor; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
            }
            Text { font.family: systemFontFamily; Layout.fillWidth: true; text: "Executável: " + root.value("exePath", "Não resolvido ou acesso restrito"); color: root.secondaryColor; font.pixelSize: Math.round(root.baseFont*(10/13)); elide: Text.ElideMiddle }
            Text { font.family: systemFontFamily; Layout.fillWidth: true; text: "Comando: " + root.value("command"); color: root.secondaryColor; font.pixelSize: Math.round(root.baseFont*(10/13)); elide: Text.ElideMiddle }
        }
    }
}
