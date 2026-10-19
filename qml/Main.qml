import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Tarefas11
import QtQuick.Dialogs as QuickDialogs
import QtCore

ApplicationWindow {
    id: root
    visible: true
    width: 1340
    height: 840
    minimumWidth: compactMode ? 320 : 1024
    minimumHeight: compactMode ? 260 : 720
    title: compactMode ? "Tarefas11 — Desempenho" : "Gerenciador de Tarefas"
    // No modo mínimo a janela é frameless; ao restaurar, Qt.Window sem esse
    // sinalizador devolve a decoração nativa do KWin. Em Wayland, AlwaysOnTop
    // continua sujeito à política do compositor.
    flags: Qt.Window | (compactMode ? Qt.FramelessWindowHint : 0)
                     | (alwaysOnTop ? Qt.WindowStaysOnTopHint : 0)
    font.family: systemFontFamily
    font.pixelSize: systemFontSize

    // Fontes e cores por padrão herdadas do Plasma através de QGuiApplication.
    property bool darkMode: systemDarkMode
    property bool useKdePalette: true
    property real fontUnit: systemFontSize
    property bool expandedMenu: true
    property bool showLogicalProcessors: false
    property bool alwaysOnTop: false
    property bool compactMode: false
    property int savedWindowWidth: 1340
    property int savedWindowHeight: 840
    property bool savedWasMaximized: false
    property bool savedWasFullscreen: false
    property string currentPage: "Processos"

    // Persistir apenas a preferência de sobreposição, não o modo compacto.
    // A geometria é mantida em memória para restaurar a janela anterior.
    Settings {
        id: windowSettings
        category: "Janela"
        property alias sempreAcima: root.alwaysOnTop
    }

    function toggleCompactMode() {
        const enteringCompact = !root.compactMode
        if (enteringCompact && root.currentPage !== "Desempenho") return
        if (enteringCompact) {
            root.savedWasMaximized = root.visibility === Window.Maximized
            root.savedWasFullscreen = root.visibility === Window.FullScreen
            root.savedWindowWidth = root.width
            root.savedWindowHeight = root.height
            if (root.savedWasMaximized || root.savedWasFullscreen) root.showNormal()
        }

        // Qt/KWin podem não recriar as bordas de uma janela já visível.
        // Trocamos FramelessWindowHint com a superfície escondida e só então
        // a apresentamos novamente, sem recriar QML ou perder a seleção.
        root.hide()
        root.compactMode = enteringCompact
        if (enteringCompact) {
            root.width = 380
            root.height = 640
        } else {
            root.width = Math.max(root.savedWindowWidth, 1024)
            root.height = Math.max(root.savedWindowHeight, 720)
        }
        root.showNormal()
        if (!enteringCompact) {
            if (root.savedWasFullscreen) root.showFullScreen()
            else if (root.savedWasMaximized) root.showMaximized()
        }
    }
    property string selectedMetric: "cpu"
    property int selectedPid: -1
    property real selectedStartTime: 0
    property string alertMessage: ""
    property string selectedServiceName: ""
    property string selectedServiceScope: ""
    property string pendingServiceAction: ""
    property string selectedStartupId: ""
    property string selectedStartupName: ""
    property color backgroundColor: useKdePalette ? platformTheme.window : (darkMode ? "#171a20" : "#f5f7fa")
    property color panelColor: useKdePalette ? platformTheme.base : (darkMode ? "#20242b" : "#ffffff")
    property color navColor: useKdePalette ? platformTheme.window : (darkMode ? "#1c2027" : "#eff3f9")
    property color inputColor: useKdePalette ? platformTheme.alternate : (darkMode ? "#292f39" : "#eef1f5")
    property color textColor: useKdePalette ? platformTheme.text : (darkMode ? "#f1f4f7" : "#1a2029")
    property color secondaryColor: useKdePalette ? platformTheme.muted : (darkMode ? "#a5b1be" : "#627282")
    property color borderColor: useKdePalette ? platformTheme.border : (darkMode ? "#39414c" : "#d7e0e8")
    property color selectedColor: useKdePalette ? platformTheme.selection : (darkMode ? "#304459" : "#dfeefa")
    property color accentColor: useKdePalette ? platformTheme.highlight : (darkMode ? "#69bdf0" : "#0078d4")
    color: backgroundColor
    palette.window: backgroundColor
    palette.base: panelColor
    palette.button: panelColor
    palette.text: textColor
    palette.buttonText: textColor
    palette.windowText: textColor
    palette.highlight: selectedColor

    property var navigation: [
        {title: "Processos", icon: "▦"},
        {title: "Desempenho", icon: "⌁"},
        {title: "Histórico de aplicativos", icon: "◷"},
        {title: "Aplicativos de inicialização", icon: "↥"},
        {title: "Usuários", icon: "♙"},
        {title: "Detalhes", icon: "≡"},
        {title: "Serviços", icon: "⚙"}
    ]
    // Lista dinâmica: cada SSD/NVMe, interface de rede e GPU DRM/PCI em vez de um item global.
    property var performanceMetrics: {
        const inventory = monitor.devices
        // SWAP e ZRAM já aparecem no painel Memória; não duplicar a entrada lateral.
        let result = [{key:"cpu", label:"CPU", hue:"#6ac1ee"},
                      {key:"memory", label:"Memória", hue:"#a28df8"}]
        for (let i = 0; i < inventory.length; ++i) {
            const d = inventory[i]
            const hue = d.kind === "disk" ? "#75b8a0" : d.kind === "net" ? "#de9c8a" : "#db92bd"
            result.push({key:d.key, label:d.label, hue:hue, kind:d.kind})
        }
        return result
    }
    function currentDevice(key) {
        const inventory = monitor.devices // Dependência para notificações de atualização
        if (key === "cpu" || key === "memory") return ({})
        return monitor.deviceInfo(key)
    }
    function kindOf(key) { return key === "cpu" || key === "memory" ? key : String(currentDevice(key).kind || "") }
    function metricHistory(key) {
        const inventory = monitor.devices // NOTIFY updated: atualiza gráficos por dispositivo
        if (key === "cpu") return monitor.cpuHistory
        if (key === "memory") return monitor.memoryHistory
        return monitor.historyFor(key)
    }
    function metricHue(key) {
        if (key === "memory") return "#a28df8"
        if (kindOf(key) === "disk") return "#75b8a0"
        if (kindOf(key) === "net") return "#de9c8a"
        if (kindOf(key) === "gpu") return "#db92bd"
        return "#6ac1ee"
    }
    function metricTitle(key) {
        if (key === "cpu") return "CPU"
        if (key === "memory") return "Memória"
        return String(currentDevice(key).label || key)
    }
    function metricDevice(key) {
        if (key === "cpu") return monitor.cpuName
        if (key === "memory") return monitor.memoryInstalledGiB > 0
            ? monitor.memoryInstalledGiB.toFixed(1) + " GiB instalados · " + monitor.memoryTotalGiB.toFixed(1) + " GiB utilizáveis"
            : monitor.memoryTotalGiB.toFixed(1) + " GiB utilizáveis · instalada requer SMBIOS"
        return String(currentDevice(key).model || "")
    }
    function metricSubtitle(key) {
        if (key === "cpu") return monitor.cpuPercent.toFixed(0) + "% · " + (monitor.cpuFrequencyGHz > 0 ? monitor.cpuFrequencyGHz.toFixed(2) + " GHz" : "frequência indisponível") + (monitor.cpuTemperatureC >= 0 ? " · " + monitor.cpuTemperatureC.toFixed(0) + " °C" : "")
        if (key === "memory") return monitor.memoryUsedGiB.toFixed(1) + "/" + monitor.memoryTotalGiB.toFixed(1) + " GiB (" + monitor.memoryPercent.toFixed(0) + "%)"
        const dev = currentDevice(key)
        if (dev.kind === "gpu") return (dev.model || "GPU não identificada") + " · " + (Number(dev.percent) >= 0 ? Number(dev.percent).toFixed(0) + "%" : "—")
        if (dev.kind === "disk") {
            const active = Number(dev.percent)
            const activity = isFinite(active) && active >= 0 ? active.toFixed(0) + "%" : "—"
            const celsius = Number(dev.temperature)
            // Temperatura ganha prioridade e não é escondida por taxas de MiB/s.
            const temp = dev.temperature !== undefined && isFinite(celsius) && celsius >= 0
                ? celsius.toFixed(0) + " °C" : "Temp. —"
            return activity + " · " + temp
        }
        if (dev.kind === "net") {
            const state = String(dev.state || "").toLowerCase()
            return state === "up" ? "Ativa" : state === "down" ? "Desconectada" : "Estado indisponível"
        }
        return String(dev.subtitle || "Sem leitura")
    }
    function metricOverview(key) {
        if (key === "cpu") return monitor.cpuPercent.toFixed(0) + "%"
        if (key === "memory") return monitor.memoryPercent.toFixed(0) + "%"
        const dev = currentDevice(key)
        if (dev.kind === "net") return (Number(dev.readMiBs || 0) + Number(dev.writeMiBs || 0)).toFixed(2) + " MiB/s"
        return Number(dev.percent) >= 0 ? Number(dev.percent).toFixed(0) + "%" : "—"
    }
    function metricMaximum(key) {
        if (key === "cpu" || key === "memory" || kindOf(key) === "disk" || kindOf(key) === "gpu") return 100
        const data = metricHistory(key)
        let maximum = 0.1
        for (let i = 0; i < data.length; ++i) maximum = Math.max(maximum, Number(data[i]))
        return maximum * 1.15
    }
    function prettyMemory(mib) { return processes.formatMemoryMiB(Number(mib)) }
    function prettyRate(mib) {
        const value = Number(mib)
        if (!isFinite(value)) return "—"
        return value < 1 ? (value * 1024).toFixed(1) + " KiB/s" : value.toFixed(2) + " MiB/s"
    }
    function uptimeText(value) {
        const n = Math.max(0, Math.floor(value))
        const d = Math.floor(n / 86400)
        const h = Math.floor(n / 3600) % 24
        const m = Math.floor(n / 60) % 60
        return d + "d " + h + "h " + m + "min"
    }
    function notify(message) {
        alertMessage = message
        notification.open()
        notificationTimer.restart()
    }

    Shortcut { sequence: "Ctrl+F"; onActivated: { currentPage = "Processos"; searchInput.forceActiveFocus() } }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (root.compactMode) root.toggleCompactMode()
            else { searchInput.text = ""; searchInput.focus = false }
        }
    }

    onCurrentPageChanged: {
        searchInput.text = ""
        processes.filterText = ""
        servicesModel.filterText = ""
        usersModel.filterText = ""
        startupModel.filterText = ""
        processes.groupMode = currentPage === "Processos"
        if (currentPage === "Detalhes") processes.typeFilter = "all"
        if (currentPage === "Serviços") servicesModel.refresh()
        if (currentPage === "Usuários") usersModel.refresh()
        if (currentPage === "Aplicativos de inicialização") startupModel.refresh()
    }
    Timer {
        interval: 9000
        repeat: true
        running: root.currentPage === "Serviços"
        onTriggered: servicesModel.refresh()
    }
    Timer {
        interval: 6000
        repeat: true
        running: root.currentPage === "Usuários"
        onTriggered: usersModel.refresh()
    }
    Connections {
        target: startupModel
        function onMessage(message) { root.notify(message) }
    }
    Connections {
        target: servicesModel
        function onMessage(message) { root.notify(message) }
    }

    Connections {
        target: processes
        function onActionError(message) { root.notify(message) }
        function onActionSuccess(message) { root.notify(message) }
        function onSelectedInfoChanged() {
            if (root.selectedPid > 0 && !processes.selectedInfo.pid) {
                root.selectedPid = -1
                root.selectedStartTime = 0
                processes.selectProcess(-1, 0)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !root.compactMode
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: root.panelColor
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                spacing: 15
                Rectangle {
                    Layout.preferredWidth: 31
                    Layout.preferredHeight: 31
                    radius: 8
                    color: root.selectedColor
                    Text { font.family: systemFontFamily; anchors.centerIn: parent; text: "▥"; color: root.accentColor; font.pixelSize: Math.round(root.fontUnit * (22/13)) }
                }
                Text { font.family: systemFontFamily; text: "Gerenciador de Tarefas"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (17/13)); font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                TextField {
                    id: searchInput
                    Layout.preferredWidth: Math.min(385, root.width * 0.35)
                    Layout.preferredHeight: 37
                    color: root.textColor
                    font.pixelSize: Math.round(root.fontUnit * (13/13))
                    placeholderText: root.currentPage === "Serviços" ? "⌕  Pesquisar serviços" :
                                     root.currentPage === "Usuários" ? "⌕  Pesquisar usuários" :
                                     root.currentPage === "Aplicativos de inicialização" ? "⌕  Pesquisar inicialização" :
                                     "⌕  Pesquisar por nome ou PID"
                    placeholderTextColor: root.secondaryColor
                    selectByMouse: true
                    leftPadding: 13
                    background: Rectangle { radius: 8; color: root.inputColor; border.color: searchInput.activeFocus ? root.accentColor : root.borderColor }
                    onTextChanged: {
                        if (root.currentPage === "Serviços") servicesModel.filterText = text
                        else if (root.currentPage === "Usuários") usersModel.filterText = text
                        else if (root.currentPage === "Aplicativos de inicialização") startupModel.filterText = text
                        else {
                            processes.filterText = text
                            if (text.length > 0 && root.currentPage !== "Processos" && root.currentPage !== "Detalhes")
                                root.currentPage = "Processos"
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Text { font.family: systemFontFamily; text: "Qt 6 · Plasma 6.8"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (11/13)) }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.borderColor }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                id: navPanel
                color: root.navColor
                Layout.preferredWidth: root.expandedMenu ? 238 : 68
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        color: "transparent"
                        radius: 8
                        Text { font.family: systemFontFamily; anchors.centerIn: parent; text: "☰"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (17/13)) }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.expandedMenu = !root.expandedMenu }
                    }
                    Item { Layout.preferredHeight: 10 }
                    Repeater {
                        model: root.navigation
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 43
                            radius: 7
                            color: root.currentPage === modelData.title ? root.selectedColor : "transparent"
                            Rectangle {
                                visible: root.currentPage === modelData.title
                                width: 3; height: 24; radius: 2
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                color: root.accentColor
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 13
                                anchors.rightMargin: 5
                                spacing: 16
                                Text { font.family: systemFontFamily;
                                    Layout.preferredWidth: 25
                                    color: root.currentPage === modelData.title ? root.accentColor : root.secondaryColor
                                    text: modelData.icon
                                    font.pixelSize: Math.round(root.fontUnit * (21/13))
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Text { font.family: systemFontFamily;
                                    visible: root.expandedMenu
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    font.pixelSize: Math.round(root.fontUnit * (13/13))
                                    color: root.textColor
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPage = modelData.title }
                            ToolTip.visible: hoverNav.hovered && !root.expandedMenu
                            ToolTip.text: modelData.title
                            HoverHandler { id: hoverNav }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        color: root.currentPage === "Configurações" ? root.selectedColor : "transparent"
                        radius: 7
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 17; spacing: 18
                            Text { font.family: systemFontFamily; text: "⚙"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (20/13)); Layout.preferredWidth: 20 }
                            Text { font.family: systemFontFamily; visible: root.expandedMenu; text: "Configurações"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (13/13)) }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPage = "Configurações" }
                    }
                    Text { font.family: systemFontFamily; Layout.fillWidth: true; visible: root.expandedMenu; horizontalAlignment: Text.AlignHCenter; text: "Tarefas11 0.4.16 · Linux + Wine"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (10/13)) }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.borderColor }

            Rectangle {
                id: bodyPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.backgroundColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    anchors.topMargin: 22
                    anchors.bottomMargin: 16
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        spacing: 10
                        Text { font.family: systemFontFamily;
                            Layout.fillWidth: true
                            text: root.currentPage
                            color: root.textColor
                            font.pixelSize: Math.round(root.fontUnit * (26/13))
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Button {
                            visible: root.currentPage === "Processos" || root.currentPage === "Detalhes"
                            id: launchButton
                            text: "+  Executar nova tarefa"
                            enabled: visible
                            onClicked: launchDialog.open()
                            padding: 10
                            contentItem: Text { font.family: systemFontFamily; text: launchButton.text; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (12/13)); verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 7; color: parent.hovered ? root.selectedColor : root.inputColor; border.color: root.borderColor }
                        }
                        Button {
                            visible: root.currentPage === "Processos" || root.currentPage === "Detalhes"
                            id: endButton
                            text: "⊗  Finalizar tarefa"
                            enabled: root.selectedPid > 1
                            onClicked: { endTaskDialog.force = false; endTaskDialog.open() }
                            padding: 10
                            contentItem: Text { font.family: systemFontFamily; text: endButton.text; color: endButton.enabled ? root.textColor : root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)); verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 7; color: parent.hovered && parent.enabled ? root.selectedColor : root.inputColor; border.color: root.borderColor; opacity: parent.enabled ? 1 : 0.5 }
                        }
                        Button {
                            visible: root.currentPage === "Processos" || root.currentPage === "Detalhes"
                            id: efficiencyButton
                            text: "♧  Modo de eficiência*"
                            enabled: root.selectedPid > 1
                            onClicked: efficiencyDialog.open()
                            padding: 10
                            contentItem: Text { font.family: systemFontFamily; text: efficiencyButton.text; color: efficiencyButton.enabled ? root.textColor : root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)); verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 7; color: parent.hovered && parent.enabled ? root.selectedColor : root.inputColor; border.color: root.borderColor; opacity: parent.enabled ? 1 : 0.5 }
                            ToolTip.visible: hovered
                            ToolTip.text: "Equivalente parcial: reduz a prioridade de CPU (nice 10), não é EcoQoS do Windows."
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.borderColor }

                    Item {
                        id: pageStack
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Item {
                            id: processPage
                            anchors.fill: parent
                            visible: root.currentPage === "Processos" || root.currentPage === "Detalhes"
                            property var columns: {
                                columnPrefs.revision
                                return columnPrefs.columns(root.currentPage === "Detalhes" ? "Detalhes" : "Processos")
                            }
                            Component.onCompleted: processes.groupMode = root.currentPage === "Processos"
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 39
                                    spacing: 12
                                    Text { font.family: systemFontFamily;
                                        Layout.fillWidth: true
                                        text: processes.totalCount + " processos · " + processes.visibleCount + " exibidos · " + processes.windowsCount + " Windows"
                                        font.pixelSize: Math.round(root.fontUnit * (12/13))
                                        color: root.secondaryColor
                                        elide: Text.ElideRight
                                    }
                                    Button {
                                        text: "Colunas ▾"
                                        Layout.preferredHeight: 34
                                        onClicked: columnPicker.open()
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Mostrar ou ocultar colunas; arraste os divisores no cabeçalho para ajustar larguras"
                                    }
                                    ComboBox {
                                        id: typeChoice
                                        Layout.preferredWidth: 240
                                        Layout.preferredHeight: 34
                                        model: ["Todos os processos", "Aplicativos Linux", "Somente programas Windows", "Sistema / segundo plano"]
                                        currentIndex: Math.max(0, ["all", "linux", "exe", "background"].indexOf(processes.typeFilter))
                                        onActivated: function(i) { processes.typeFilter = ["all", "linux", "exe", "background"][i] }
                                        // O estilo org.kde.desktop cria um TextField nativo e
                                        // utiliza positionToRectangle() internamente. Sobrescrever
                                        // com Text gerava o TypeError no ComboBox do KDE.
                                        font.family: systemFontFamily
                                        font.pixelSize: Math.round(root.fontUnit * (12/13))
                                        background: Rectangle {
                                            color: root.panelColor
                                            border.color: root.borderColor
                                            radius: 9
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: "No modo Processos, os itens são agrupados por categoria e por processo pai."
                                    }
                                }
                                Rectangle {
                                    id: tableHeader
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    color: root.panelColor
                                    border.color: root.borderColor
                                    radius: 8
                                    // Em vez de Menu com 16 linhas verticais, seletor em
                                    // duas colunas, compacto e com limite de altura.
                                    Popup {
                                        id: columnPicker
                                        parent: processPage
                                        x: Math.max(8, processPage.width - width - 8)
                                        y: 72
                                        width: Math.min(420, processPage.width - 16)
                                        height: Math.min(326, processPage.height - 95)
                                        padding: 12
                                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                                        background: Rectangle {
                                            radius: 10
                                            color: root.panelColor
                                            border.color: root.borderColor
                                        }
                                        contentItem: ColumnLayout {
                                            spacing: 7
                                            Text {
                                                text: "Escolher colunas"
                                                font.family: root.font.family
                                                font.bold: true
                                                font.pixelSize: Math.round(root.fontUnit * (14 / 13))
                                                color: root.textColor
                                            }
                                            ScrollView {
                                                id: pickerScroll
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                clip: true
                                                contentWidth: availableWidth
                                                GridLayout {
                                                    width: pickerScroll.availableWidth
                                                    columns: 2
                                                    columnSpacing: 8
                                                    rowSpacing: 0
                                                    Repeater {
                                                        model: { columnPrefs.revision; return columnPrefs.availableColumns(root.currentPage === "Detalhes" ? "Detalhes" : "Processos") }
                                                        delegate: CheckBox {
                                                            required property var modelData
                                                            Layout.fillWidth: true
                                                            Layout.minimumWidth: 0
                                                            text: modelData.title
                                                            checked: modelData.enabled
                                                            enabled: !modelData.mandatory
                                                            font.family: root.font.family
                                                            font.pixelSize: Math.round(root.fontUnit * (11 / 13))
                                                            implicitHeight: 28
                                                            onClicked: columnPrefs.setVisible(root.currentPage === "Detalhes" ? "Detalhes" : "Processos", modelData.key, checked)
                                                        }
                                                    }
                                                }
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                Item { Layout.fillWidth: true }
                                                Button {
                                                    text: "Restaurar padrão"
                                                    onClicked: columnPrefs.resetColumns(root.currentPage === "Detalhes" ? "Detalhes" : "Processos")
                                                }
                                                Button { text: "Fechar"; onClicked: columnPicker.close() }
                                            }
                                        }
                                    }
                                    Row {
                                        anchors.fill: parent
                                        Repeater {
                                            model: processPage.columns
                                            delegate: Rectangle {
                                                required property var modelData
                                                required property int index
                                                width: tableHeader.width * modelData.ratio
                                                height: tableHeader.height
                                                color: "transparent"
                                                Rectangle {
                                                    anchors.right: parent.right
                                                    width: 1; height: parent.height
                                                    color: root.borderColor
                                                    opacity: 0.65
                                                }
                                                Column {
                                                    width: Math.max(0, parent.width - 24)
                                                    anchors.centerIn: parent
                                                    spacing: 3
                                                    Text {
                                                        font.family: root.font.family
                                                        width: parent.width
                                                        visible: root.currentPage === "Processos" && ["cpu","mem","swap","disk","net"].indexOf(modelData.key) >= 0
                                                        horizontalAlignment: Text.AlignHCenter
                                                        text: modelData.key === "cpu" ? monitor.cpuPercent.toFixed(0) + "%"
                                                              : modelData.key === "mem" ? monitor.memoryPercent.toFixed(0) + "%"
                                                              : modelData.key === "swap" ? (monitor.swapTotalGiB > 0 ? (100 * monitor.swapUsedGiB / monitor.swapTotalGiB).toFixed(0) + "%" : "—")
                                                              : modelData.key === "disk" ? monitor.diskBusyPercent.toFixed(0) + "%"
                                                              : root.prettyRate(monitor.netRxMBs + monitor.netTxMBs)
                                                        color: root.accentColor
                                                        font.pixelSize: modelData.key === "net" ? Math.max(11, Math.round(root.fontUnit * 0.9)) : Math.max(15, Math.round(root.fontUnit * 1.2))
                                                        font.weight: Font.DemiBold
                                                        renderType: Text.NativeRendering
                                                        elide: Text.ElideRight
                                                    }
                                                    Text {
                                                        font.family: root.font.family
                                                        width: parent.width
                                                        text: (modelData.key === "cpu" ? "CPU" : modelData.key === "disk" ? "Disco" : modelData.title) + (processes.sortField === (modelData.key === "mem" ? "memory" : modelData.key === "net" ? "network" : modelData.key) ? (modelData.key === "name" ? " ↑" : " ▾") : "")
                                                        color: root.textColor
                                                        font.pixelSize: Math.max(12, Math.round(root.fontUnit * 1.0))
                                                        font.weight: Font.DemiBold
                                                        font.letterSpacing: 0.1
                                                        renderType: Text.NativeRendering
                                                        horizontalAlignment: modelData.key === "name" ? Text.AlignLeft : Text.AlignHCenter
                                                        elide: Text.ElideRight
                                                        ToolTip.visible: labelHover.hovered && truncated
                                                        ToolTip.text: modelData.title
                                                        HoverHandler { id: labelHover }
                                                    }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    onClicked: function(mouse) {
                                                        if (mouse.button === Qt.RightButton) { columnPicker.open(); return }
                                                        const key = modelData.key
                                                        const field = key === "mem" ? "memory" : key === "diskBusy" ? "ioshare" : key === "net" ? "network" : key
                                                        processes.sortField = field
                                                    }
                                                }
                                                // Divisor arrastável. Ajusta esta coluna e a próxima, salvando no QSettings.
                                                Rectangle {
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.bottom: parent.bottom
                                                    width: 9
                                                    color: resizeHover.hovered ? root.accentColor : "transparent"
                                                    opacity: resizeHover.hovered ? 0.5 : 0
                                                    visible: index < processPage.columns.length - 1
                                                    HoverHandler { id: resizeHover }
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        enabled: parent.visible
                                                        cursorShape: Qt.SizeHorCursor
                                                        property real lastPointerX: 0
                                                        onPressed: function(mouse) { lastPointerX = mapToItem(tableHeader, mouse.x, mouse.y).x }
                                                        onReleased: function(mouse) {
                                                            const at = mapToItem(tableHeader, mouse.x, mouse.y).x
                                                            const delta = at - lastPointerX
                                                            if (Math.abs(delta) < 1) return
                                                            columnPrefs.resizeColumn(root.currentPage === "Detalhes" ? "Detalhes" : "Processos",
                                                                modelData.key, delta, tableHeader.width)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                ListView {
                                    id: listProcesses
                                    reuseItems: true
                                    cacheBuffer: 160
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: processes
                                    boundsBehavior: Flickable.StopAtBounds
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    // Guarda o item visível + deslocamento; updates normais do /proc
                                    // não disparam reset no C++, e alterações estruturais mantêm a âncora.
                                    property string scrollAnchor: ""
                                    property real scrollOffset: 0
                                    property real previousScrollY: 0
                                    property int restoreSerial: 0
                                    Connections {
                                        target: processes
                                        function onModelAboutToBeReset() {
                                            listProcesses.previousScrollY = Math.max(0, listProcesses.contentY)
                                            const idx = listProcesses.indexAt(5, listProcesses.previousScrollY + 4)
                                            listProcesses.scrollAnchor = processes.rowKeyAt(idx)
                                            const item = idx >= 0 ? listProcesses.itemAtIndex(idx) : null
                                            listProcesses.scrollOffset = item ? listProcesses.previousScrollY - item.y : 0
                                        }
                                        function onModelReset() {
                                            const key = listProcesses.scrollAnchor
                                            const idx = key ? processes.indexOfRow(key) : -1
                                            const oldY = listProcesses.previousScrollY
                                            const offset = listProcesses.scrollOffset
                                            const serial = ++listProcesses.restoreSerial
                                            Qt.callLater(function() {
                                                if (serial !== listProcesses.restoreSerial || !processPage.visible) return
                                                if (idx >= 0) {
                                                    listProcesses.positionViewAtIndex(idx, ListView.Beginning)
                                                    const item = listProcesses.itemAtIndex(idx)
                                                    if (item) {
                                                        listProcesses.contentY = Math.max(0, Math.min(item.y + offset,
                                                            Math.max(0, listProcesses.contentHeight - listProcesses.height)))
                                                        return
                                                    }
                                                }
                                                listProcesses.contentY = Math.max(0, Math.min(oldY,
                                                    Math.max(0, listProcesses.contentHeight - listProcesses.height)))
                                            })
                                        }
                                    }
                                    // Delegate direto: Loader/Component isolava os roles de 'model' na 0.3.0.
                                    delegate: Rectangle {
                                        id: processRow
                                        width: listProcesses.width
                                        height: rowHeader ? 42 : 43
                                        property bool rowHeader: model.isHeader === true
                                        property string categoryLabel: model.categoryTitle || ""
                                        property string categoryKey: model.categoryKey || ""
                                        property bool categoryExpanded: model.isHeader === true && model.expanded === true
                                        property int rowPid: rowHeader ? -1 : Number(model.pid)
                                        property real rowStart: rowHeader ? 0 : Number(model.startTime)
                                        property int rowLevel: rowHeader ? 0 : Number(model.level)
                                        property bool rowExpandable: !rowHeader && model.expandable === true
                                        property bool rowExpanded: !rowHeader && model.expanded === true
                                        property int rowChildren: rowHeader ? 0 : Number(model.childCount)
                                        property string rowName: rowHeader ? "" : String(model.displayName || model.processName || "")
                                        property string rowIcon: rowHeader ? "" : String(model.iconUrl || "")
                                        property string rowPlatform: rowHeader ? "" : String(model.platform || "")
                                        property string rowCommand: rowHeader ? "" : String(model.command || "")
                                        property string rowIconSource: rowHeader ? "" : String(model.iconSource || "")
                                        property string rowState: rowHeader ? "" : String(model.processState || "")
                                        property string rowUser: rowHeader ? "" : String(model.processUser || "")
                                        property real rowCpu: rowHeader ? 0 : Number(model.cpu || 0)
                                        property real rowMemory: rowHeader ? 0 : Number(model.memoryMiB || 0)
                                        property bool rowSwapAvailable: !rowHeader && model.swapAvailable === true
                                        property real rowSwap: rowHeader ? 0 : Number(model.swapMiB || 0)
                                        property bool rowAggregated: !rowHeader && model.aggregated === true
                                        property int rowGroupCount: rowHeader ? 0 : Number(model.groupProcessCount || 1)
                                        property real rowOwnMemory: rowHeader ? 0 : Number(model.ownMemoryMiB || 0)
                                        property real rowDisk: rowHeader ? 0 : Number(model.diskMiBs || 0)
                                        property real rowIoShare: rowHeader ? 0 : Number(model.ioSharePercent || 0)
                                        property bool rowNetAvailable: !rowHeader && model.networkAvailable === true
                                        property real rowNet: rowHeader ? 0 : Number(model.networkMiBs || 0)
                                        property real rowNetRx: rowHeader ? 0 : Number(model.networkRxMiBs || 0)
                                        property real rowNetTx: rowHeader ? 0 : Number(model.networkTxMiBs || 0)
                                        property real rowRead: rowHeader ? 0 : Number(model.readMiBs || 0)
                                        property real rowWrite: rowHeader ? 0 : Number(model.writeMiBs || 0)
                                        property real rowVirtual: rowHeader ? 0 : Number(model.virtualMiB || 0)
                                        property bool rowIo: !rowHeader && model.ioAvailable === true
                                        property int rowPpid: rowHeader ? -1 : Number(model.ppid)
                                        property int rowThreads: rowHeader ? 0 : Number(model.threads)
                                        property int rowNice: rowHeader ? 0 : Number(model.niceValue)

                                        color: rowHeader ? root.backgroundColor :
                                            (root.selectedPid === rowPid && root.selectedStartTime === rowStart
                                             ? root.selectedColor : (index % 2 ? root.backgroundColor : root.panelColor))
                                        border.color: root.borderColor
                                        border.width: !rowHeader && root.selectedPid === rowPid && root.selectedStartTime === rowStart ? 1 : 0
                                        radius: rowHeader ? 0 : 5

                                        function cellText(key) {
                                            if (key === "name") return rowName + (rowExpandable && root.currentPage === "Processos" ? " (" + rowChildren + ")" : "")
                                            if (key === "pid") return String(rowPid)
                                            if (key === "ppid") return String(rowPpid)
                                            if (key === "nice") return String(rowNice)
                                            if (key === "virtual") return root.prettyMemory(rowVirtual)
                                            if (key === "user") return rowUser
                                            if (key === "state") return rowState
                                            if (key === "threads") return String(rowThreads)
                                            if (key === "cpu") return rowCpu.toFixed(1) + "%"
                                            if (key === "mem") return root.prettyMemory(rowMemory)
                                            if (key === "swap") return rowSwapAvailable ? root.prettyMemory(rowSwap) : "—"
                                            if (key === "disk") return rowIo ? root.prettyRate(rowDisk) : "—"
                                            if (key === "diskBusy") return rowIo ? rowIoShare.toFixed(1) + "%" : "—"
                                            if (key === "read") return rowIo ? root.prettyRate(rowRead) : "—"
                                            if (key === "write") return rowIo ? root.prettyRate(rowWrite) : "—"
                                            if (key === "platform") return rowPlatform
                                            if (key === "net") return rowNetAvailable ? root.prettyRate(rowNet) : "—"
                                            return ""
                                        }
                                        Row {
                                            visible: processRow.rowHeader
                                            anchors.left: parent.left
                                            anchors.leftMargin: 11
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 9
                                            Text { font.family: systemFontFamily;
                                                text: processRow.categoryExpanded ? "▾" : "▸"
                                                color: root.secondaryColor
                                                font.pixelSize: Math.round(root.fontUnit * (17/13))
                                                width: 17
                                            }
                                            Text { font.family: systemFontFamily;
                                                text: processRow.categoryLabel
                                                color: root.textColor
                                                font.pixelSize: Math.round(root.fontUnit * (15/13))
                                                font.weight: Font.DemiBold
                                            }
                                        }
                                        MouseArea {
                                            visible: processRow.rowHeader
                                            anchors.fill: parent
                                            z: 3
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: processes.toggleCategory(processRow.categoryKey)
                                            ToolTip.visible: headerHover.hovered
                                            ToolTip.text: processRow.categoryExpanded ? "Recolher categoria" : "Expandir categoria"
                                            HoverHandler { id: headerHover }
                                        }
                                        // Área geral de seleção fica ATRÁS da seta, sem capturar seu clique.
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !processRow.rowHeader
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: function(mouse) {
                                                root.selectedPid = processRow.rowPid
                                                root.selectedStartTime = processRow.rowStart
                                                processes.selectProcess(processRow.rowPid, processRow.rowStart)
                                                if (mouse.button === Qt.RightButton) rowMenu.popup()
                                            }
                                            onDoubleClicked: function(mouse) {
                                                if (mouse.button !== Qt.LeftButton) return
                                                if (processRow.rowExpandable && root.currentPage === "Processos")
                                                    processes.toggleExpanded(processRow.rowPid, processRow.rowStart)
                                                else root.currentPage = "Detalhes"
                                            }
                                        }
                                        Row {
                                            visible: !processRow.rowHeader
                                            anchors.fill: parent
                                            Repeater {
                                                model: processPage.columns
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    width: processRow.width * modelData.ratio
                                                    height: processRow.height
                                                    color: modelData.key === "cpu" && processRow.rowCpu > 0
                                                        ? Qt.rgba(0.0, 0.47, 0.83, Math.min(0.25, 0.05 + processRow.rowCpu / 500))
                                                        : modelData.key === "mem" && processRow.rowMemory > 80
                                                            ? Qt.rgba(0.0, 0.47, 0.83, Math.min(0.18, 0.04 + processRow.rowMemory / 12000))
                                                            : "transparent"
                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: modelData.key === "name" ? (10 + processRow.rowLevel * 17) : 10
                                                        anchors.rightMargin: 8
                                                        spacing: 7
                                                        Item {
                                                            visible: modelData.key === "name"
                                                            Layout.preferredWidth: 16
                                                            Layout.preferredHeight: 24
                                                            Text { font.family: systemFontFamily;
                                                                anchors.centerIn: parent
                                                                visible: root.currentPage === "Processos" && processRow.rowExpandable
                                                                text: processRow.rowExpanded ? "▾" : "▸"
                                                                color: root.secondaryColor
                                                                font.pixelSize: Math.round(root.fontUnit * (16/13))
                                                            }
                                                            MouseArea {
                                                                anchors.fill: parent
                                                                z: 2
                                                                enabled: root.currentPage === "Processos" && processRow.rowExpandable
                                                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                                onClicked: processes.toggleExpanded(processRow.rowPid, processRow.rowStart)
                                                            }
                                                        }
                                                        Image {
                                                            visible: modelData.key === "name"
                                                            Layout.preferredWidth: 22
                                                            Layout.preferredHeight: 22
                                                            sourceSize: Qt.size(44, 44)
                                                            source: processRow.rowIcon
                                                            fillMode: Image.PreserveAspectFit
                                                            smooth: true
                                                        }
                                                        Text {
                                                            font.family: root.font.family
                                                            Layout.fillWidth: true
                                                            color: root.textColor
                                                            font.pixelSize: Math.max(12, Math.round(root.fontUnit * 1.0))
                                                            font.weight: modelData.key === "name" ? Font.Medium : Font.Normal
                                                            renderType: Text.NativeRendering
                                                            text: processRow.cellText(modelData.key)
                                                            horizontalAlignment: modelData.key === "name" || modelData.key === "state" || modelData.key === "user" ? Text.AlignLeft : Text.AlignRight
                                                            verticalAlignment: Text.AlignVCenter
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        HoverHandler { id: processHover; enabled: !processRow.rowHeader }
                                        ToolTip.visible: processHover.hovered && !processRow.rowHeader
                                        ToolTip.delay: 900
                                        ToolTip.text: processRow.rowName + " · PID " + processRow.rowPid + "\n" + processRow.rowPlatform +
                                                      "\nRAM residente (RSS): " + root.prettyMemory(processRow.rowMemory) +
                                                      "\nSWAP privada (VmSwap): " + (processRow.rowSwapAvailable ? root.prettyMemory(processRow.rowSwap) : "não disponível") +
                                                      (processRow.rowAggregated ? "\nGrupo: " + processRow.rowGroupCount +
                                                       " processos; RSS somada estimada (memória compartilhada pode repetir)." : "") +
                                                      "\n" + processRow.rowIconSource + "\n" + processRow.rowCommand
                                        Menu {
                                            id: rowMenu
                                            MenuItem { text: "Finalizar tarefa"; onTriggered: { endTaskDialog.force = false; endTaskDialog.open() } }
                                            MenuItem { text: "Forçar encerramento (SIGKILL)"; onTriggered: { endTaskDialog.force = true; endTaskDialog.open() } }
                                            MenuSeparator {}
                                            MenuItem { text: "Reduzir prioridade de CPU"; onTriggered: efficiencyDialog.open() }
                                            MenuItem { text: "Copiar PID"; onTriggered: processes.copyPid(root.selectedPid) }
                                            MenuItem { text: "Copiar comando"; onTriggered: processes.copyCommand(root.selectedPid, root.selectedStartTime) }
                                            MenuItem { text: "Abrir pasta do executável"; onTriggered: processes.openContainingFolder(root.selectedPid, root.selectedStartTime) }
                                        }
                                    }
                                }
                                ProcessInfoPanel {
                                    visible: root.selectedPid > 0 && processes.selectedInfo.pid !== undefined
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: visible ? 230 : 0
                                    info: processes.selectedInfo
                                    baseFont: root.fontUnit
                                    panelColor: root.panelColor
                                    borderColor: root.borderColor
                                    textColor: root.textColor
                                    secondaryColor: root.secondaryColor
                                    accentColor: root.accentColor
                                    backgroundColor: root.backgroundColor
                                }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 28; color: "transparent"
                                    Text { font.family: systemFontFamily;
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root.currentPage === "Processos"
                                              ? "RAM = RSS residente; SWAP = VmSwap (páginas privadas) · Grupos somam os filhos (RSS compartilhada pode repetir)"
                                              : "Detalhes: valores individuais por PID · RAM residente ≠ SWAP (VmSwap) · Unidades MB/GB decimais"
                                        font.pixelSize: Math.round(root.fontUnit * (10/13))
                                        color: root.secondaryColor
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }
                            }
                        }
                        Item {
                            id: performancePage
                            anchors.fill: parent
                            visible: root.currentPage === "Desempenho"
                            RowLayout {
                                anchors.fill: parent
                                spacing: 18
                                ScrollView {
                                    // Mantém a mesma largura ao alternar CPU/Memória/Discos/GPU;
                                    // 174 px cortava nomes e descrições. A largura responsiva
                                    // reserva espaço ao painel de Memória, sem rolagem interna.
                                    Layout.preferredWidth: Math.max(238, Math.min(284, pageStack.width * 0.26))
                                    Layout.fillHeight: true
                                    contentWidth: availableWidth
                                    clip: true
                                    Column {
                                        width: parent.width
                                        spacing: 8
                                        Repeater {
                                            model: root.performanceMetrics
                                            delegate: MetricTile {
                                                required property var modelData
                                                width: parent ? parent.width : 240
                                                metricTitle: modelData.label
                                                subtitle: root.metricSubtitle(modelData.key)
                                                points: root.metricHistory(modelData.key)
                                                accentColor: modelData.hue
                                                primaryText: root.textColor
                                                baseFont: root.fontUnit
                                                secondaryText: root.secondaryColor
                                                frameColor: root.borderColor
                                                selected: root.selectedMetric === modelData.key
                                                percentageGraph: modelData.kind !== "net"
                                                onClicked: root.selectedMetric = modelData.key
                                                onDoubleClicked: root.toggleCompactMode()
                                            }
                                        }
                                    }
                                }
                                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.borderColor }
                                MemoryDashboard {
                                    visible: root.selectedMetric === "memory"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    telemetry: monitor
                                    panelColor: root.panelColor
                                    borderColor: root.borderColor
                                    textColor: root.textColor
                                    secondaryColor: root.secondaryColor
                                    accentColor: root.accentColor
                                    baseFont: root.fontUnit
                                    onRequestHardware: monitor.requestMemoryHardwareDetails()
                                }
                                GpuDashboard {
                                    visible: root.kindOf(root.selectedMetric) === "gpu"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    telemetry: monitor
                                    device: root.currentDevice(root.selectedMetric)
                                    deviceKey: root.selectedMetric
                                    history: root.metricHistory(root.selectedMetric)
                                    panelColor: root.panelColor
                                    borderColor: root.borderColor
                                    textColor: root.textColor
                                    secondaryColor: root.secondaryColor
                                    accentColor: root.metricHue(root.selectedMetric)
                                    baseFont: root.fontUnit
                                    systemFontFamily: root.font.family
                                }
                                ScrollView {
                                    id: performanceScroll
                                    visible: root.selectedMetric !== "memory" && root.kindOf(root.selectedMetric) !== "gpu"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    contentWidth: availableWidth
                                    ColumnLayout {
                                        width: performanceScroll.availableWidth - 12
                                        spacing: 14
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { font.family: systemFontFamily;
                                                text: root.metricTitle(root.selectedMetric)
                                                color: root.textColor
                                                font.pixelSize: Math.round(root.fontUnit * (29/13))
                                                font.weight: Font.DemiBold
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text { font.family: systemFontFamily;
                                                text: root.metricDevice(root.selectedMetric)
                                                color: root.secondaryColor
                                                font.pixelSize: Math.round(root.fontUnit * (12/13))
                                                Layout.maximumWidth: 410
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Text { font.family: systemFontFamily;
                                            text: root.selectedMetric === "cpu" || root.selectedMetric === "memory" || root.kindOf(root.selectedMetric) === "disk" || root.kindOf(root.selectedMetric) === "gpu"
                                                ? "% de atividade nos últimos 60 registros" : "Tráfego (MiB/s) nos últimos 60 registros"
                                            font.pixelSize: Math.round(root.fontUnit * (12/13))
                                            color: root.secondaryColor
                                        }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 315
                                            color: root.panelColor
                                            border.color: root.borderColor
                                            HistoryGraph {
                                                anchors.fill: parent
                                                anchors.margins: 7
                                                samples: root.metricHistory(root.selectedMetric)
                                                graphColor: root.metricHue(root.selectedMetric)
                                                fillColor: Qt.rgba(0.28, 0.58, 0.84, 0.18)
                                                gridColor: root.borderColor
                                                maxValue: root.metricMaximum(root.selectedMetric)
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { font.family: systemFontFamily; text: "60 amostras (a taxa de atualização é configurável)"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (11/13)) }
                                            Item { Layout.fillWidth: true }
                                            Text { font.family: systemFontFamily; text: root.metricOverview(root.selectedMetric); color: root.accentColor; font.pixelSize: Math.round(root.fontUnit * (24/13)); font.weight: Font.DemiBold }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.borderColor }
                                        ColumnLayout {
                                            visible: root.selectedMetric === "memory"
                                            Layout.fillWidth: true
                                            spacing: 8
                                            Text { font.family: systemFontFamily; text: "Composição da memória física"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (13/13)); font.bold: true }
                                            Row {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 36
                                                spacing: 1
                                                property real full: Math.max(0.001, monitor.memoryTotalGiB)
                                                property real used: Math.max(0, Math.min(1, monitor.memoryUsedGiB / full))
                                                property real reclaimable: Math.max(0, Math.min(1-used, (monitor.memoryAvailableGiB - monitor.memoryFreeGiB) / full))
                                                Rectangle { width: Math.max(0, parent.width * parent.used - 1); height: parent.height; color: "#5e98dc"; border.color: root.borderColor }
                                                Rectangle { width: Math.max(0, parent.width * parent.reclaimable - 1); height: parent.height; color: "#b6d2f4"; border.color: root.borderColor }
                                                Rectangle { width: Math.max(0, parent.width * Math.max(0, 1-parent.used-parent.reclaimable) - 2); height: parent.height; color: root.panelColor; border.color: root.borderColor }
                                            }
                                            Text { font.family: systemFontFamily;
                                                text: "Em uso: " + monitor.memoryUsedGiB.toFixed(2) + " GiB   ·   Disponível recuperável: " + Math.max(0,monitor.memoryAvailableGiB-monitor.memoryFreeGiB).toFixed(2) + " GiB   ·   Livre: " + monitor.memoryFreeGiB.toFixed(2) + " GiB"
                                                color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (11/13)); wrapMode: Text.WordWrap; Layout.fillWidth: true
                                            }
                                            Text { font.family: systemFontFamily; text: "Swap (área de troca — inclui ZRAM quando configurada)"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (13/13)); font.bold: true }
                                            Row {
                                                Layout.fillWidth: true; Layout.preferredHeight: 22; spacing: 1
                                                Rectangle {
                                                    width: parent.width * Math.min(1, monitor.swapUsedGiB / Math.max(0.001, monitor.swapTotalGiB))
                                                    height: parent.height; color: "#9b86cf"; border.color: root.borderColor
                                                }
                                                Rectangle {
                                                    width: Math.max(0,parent.width * (1-Math.min(1,monitor.swapUsedGiB / Math.max(0.001,monitor.swapTotalGiB))) - 2)
                                                    height: parent.height; color: root.panelColor; border.color: root.borderColor
                                                }
                                            }
                                            Text { font.family: systemFontFamily;
                                                text: monitor.swapUsedGiB.toFixed(2) + " / " + monitor.swapTotalGiB.toFixed(2) + " GiB · ZRAM lógica " + monitor.zramOriginalGiB.toFixed(2) + " GiB → comprimida " + monitor.zramCompressedGiB.toFixed(2) + " GiB (ocupação física " + monitor.zramPhysicalGiB.toFixed(2) + " GiB)"
                                                color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (11/13)); wrapMode: Text.WordWrap; Layout.fillWidth: true
                                            }
                                        }
                                        RowLayout {
                                            visible: root.selectedMetric === "memory"
                                            Layout.fillWidth: true
                                            Button {
                                                text: "Consultar módulos RAM (Polkit/SMBIOS)"
                                                onClicked: monitor.requestMemoryHardwareDetails()
                                                ToolTip.visible: hovered
                                                ToolTip.text: "Opcional: pede autorização para executar dmidecode. Não altera hardware."
                                            }
                                            Text { font.family: systemFontFamily; Layout.fillWidth: true; text: monitor.memoryModules; font.pixelSize: Math.round(root.fontUnit * (11/13)); color: root.secondaryColor; elide: Text.ElideRight }
                                        }
                                        GridLayout {
                                            visible: root.kindOf(root.selectedMetric) === "gpu"
                                            Layout.fillWidth: true
                                            columns: 2
                                            rowSpacing: 10
                                            columnSpacing: 12
                                            Repeater {
                                                model: [
                                                    {label:"3D / render", key:"renderPercent"},
                                                    {label:"Copy", key:"copyPercent"},
                                                    {label:"Video Encode", key:"encodePercent"},
                                                    {label:"Video Decode", key:"decodePercent"}
                                                ]
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 132
                                                    color: root.panelColor
                                                    border.color: root.borderColor
                                                    radius: 7
                                                    ColumnLayout {
                                                        anchors.fill: parent; anchors.margins: 10; spacing: 7
                                                        Text { font.family: systemFontFamily; text: modelData.label; font.pixelSize: Math.round(root.fontUnit * (12/13)); color: root.secondaryColor }
                                                        Text { font.family: systemFontFamily;
                                                            text: Number(root.currentDevice(root.selectedMetric)[modelData.key]) >= 0
                                                                  ? Number(root.currentDevice(root.selectedMetric)[modelData.key]).toFixed(1) + "%"
                                                                  : "Indisponível"
                                                            color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (15/13)); font.bold: true
                                                        }
                                                        HistoryGraph {
                                                            Layout.fillWidth: true
                                                            Layout.fillHeight: true
                                                            samples: { monitor.devices; return monitor.historyFor(root.selectedMetric + "#" + modelData.key) }
                                                            graphColor: root.metricHue(root.selectedMetric)
                                                            fillColor: "#199777bc"
                                                            gridColor: root.borderColor
                                                            maxValue: 100
                                                            showGrid: false
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        Text { font.family: systemFontFamily;
                                            visible: root.kindOf(root.selectedMetric) === "gpu"
                                            Layout.fillWidth: true
                                            color: root.secondaryColor
                                            font.pixelSize: Math.round(root.fontUnit * (11/13))
                                            wrapMode: Text.WordWrap
                                            text: "Engines: NVIDIA NVML (vídeo), DRM fdinfo (clientes acessíveis) ou indisponível. DRM pode não representar todos os processos do sistema; Copy/3D não são inferidos a partir do uso global."
                                        }
                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 3
                                            columnSpacing: 24
                                            rowSpacing: 15
                                            Repeater {
                                                model: root.selectedMetric === "cpu" ? [
                                                    {label:"Utilização", value:monitor.cpuPercent.toFixed(1) + "%"},
                                                    {label:"Velocidade média atual", value:monitor.cpuFrequencyGHz > 0 ? monitor.cpuFrequencyGHz.toFixed(2) + " GHz" : "—"},
                                                    {label:"Temperatura (sensor CPU)", value:monitor.cpuTemperatureC >= 0 ? monitor.cpuTemperatureC.toFixed(1) + " °C" : "— (sem sensor hwmon)"},
                                                    {label:"Frequência base", value:monitor.cpuBaseGHz > 0 ? monitor.cpuBaseGHz.toFixed(2) + " GHz" : "—"},
                                                    {label:"Frequência máxima informada", value:monitor.cpuMaxGHz > 0 ? monitor.cpuMaxGHz.toFixed(2) + " GHz" : "—"},
                                                    {label:"Processos", value:String(processes.totalCount)},
                                                    {label:"Threads", value:String(processes.threadCount)},
                                                    {label:"Descritores de arquivos (sistema)", value:String(monitor.systemOpenFiles)},
                                                    {label:"Núcleos físicos", value:monitor.physicalCoreCount > 0 ? String(monitor.physicalCoreCount) : "—"},
                                                    {label:"Processadores lógicos", value:String(monitor.coreCount)},
                                                    {label:"Soquetes", value:String(monitor.cpuSockets)},
                                                    {label:"Cache L1 total", value:monitor.cpuCacheL1},
                                                    {label:"Cache L2 total", value:monitor.cpuCacheL2},
                                                    {label:"Cache L3", value:monitor.cpuCacheL3},
                                                    {label:"Virtualização", value:monitor.cpuVirtualization},
                                                    {label:"Arquitetura", value:monitor.cpuArchitecture},
                                                    {label:"Tempo de atividade", value:root.uptimeText(monitor.uptimeSeconds)}
                                                ] : root.selectedMetric === "memory" ? [
                                                    {label:"Em uso (estimado)", value:monitor.memoryUsedGiB.toFixed(2) + " GiB"},
                                                    {label:"Total utilizável", value:monitor.memoryTotalGiB.toFixed(2) + " GiB"},
                                                    {label:"Disponível", value:monitor.memoryAvailableGiB.toFixed(2) + " GiB"},
                                                    {label:"Livre", value:monitor.memoryFreeGiB.toFixed(2) + " GiB"},
                                                    {label:"Cache de arquivos", value:monitor.memoryCachedGiB.toFixed(2) + " GiB"},
                                                    {label:"Buffers", value:monitor.memoryBuffersGiB.toFixed(2) + " GiB"},
                                                    {label:"Slab kernel", value:monitor.memorySlabGiB.toFixed(2) + " GiB"},
                                                    {label:"Memória compartilhada", value:monitor.memorySharedGiB.toFixed(2) + " GiB"},
                                                    {label:"Ativa", value:monitor.memoryActiveGiB.toFixed(2) + " GiB"},
                                                    {label:"Comprometida", value:monitor.memoryCommittedGiB.toFixed(2) + " GiB"},
                                                    {label:"Swap", value:monitor.swapUsedGiB.toFixed(2) + " / " + monitor.swapTotalGiB.toFixed(2) + " GiB"},
                                                    {label:"ZRAM lógica", value:monitor.zramOriginalGiB.toFixed(2) + " GiB"},
                                                    {label:"ZRAM comprimida", value:monitor.zramCompressedGiB.toFixed(2) + " GiB"},
                                                    {label:"ZRAM custo físico", value:monitor.zramPhysicalGiB.toFixed(2) + " GiB"},
                                                    {label:"Tipo da RAM", value:monitor.memoryType},
                                                    {label:"Velocidade da RAM", value:monitor.memorySpeed}
                                                ] : root.kindOf(root.selectedMetric) === "disk" ? [
                                                    {label:"Tempo ativo", value:root.metricOverview(root.selectedMetric)},
                                                    {label:"Temperatura (sensor disco)", value:root.currentDevice(root.selectedMetric).temperature !== undefined && Number(root.currentDevice(root.selectedMetric).temperature) >= 0 ? Number(root.currentDevice(root.selectedMetric).temperature).toFixed(1) + " °C" : "— (hwmon / S.M.A.R.T. indisponível)"},
                                                    {label:"Tempo médio por E/S", value:Number(root.currentDevice(root.selectedMetric).responseMs) >= 0 ? Number(root.currentDevice(root.selectedMetric).responseMs).toFixed(2) + " ms (estimado)" : "— (sem E/S concluídas)"},
                                                    {label:"Leituras/s", value:Number(root.currentDevice(root.selectedMetric).readIops || 0).toFixed(0) + " IOPS"},
                                                    {label:"Gravações/s", value:Number(root.currentDevice(root.selectedMetric).writeIops || 0).toFixed(0) + " IOPS"},
                                                    {label:"Taxa de leitura", value:root.prettyRate(root.currentDevice(root.selectedMetric).readMiBs)},
                                                    {label:"Taxa de gravação", value:root.prettyRate(root.currentDevice(root.selectedMetric).writeMiBs)},
                                                    {label:"Capacidade", value:Number(root.currentDevice(root.selectedMetric).capacityGiB || 0).toFixed(1) + " GiB"},
                                                    {label:"Tipo", value:String(root.currentDevice(root.selectedMetric).media || "—")},
                                                    {label:"Barramento", value:String(root.currentDevice(root.selectedMetric).bus || "—")},
                                                    {label:"Dispositivo", value:String(root.currentDevice(root.selectedMetric).device || "—")}
                                                ] : root.kindOf(root.selectedMetric) === "net" ? [
                                                    {label:"Recebendo", value:root.prettyRate(root.currentDevice(root.selectedMetric).readMiBs)},
                                                    {label:"Enviando", value:root.prettyRate(root.currentDevice(root.selectedMetric).writeMiBs)},
                                                    {label:"Conexão / rede", value:String(root.currentDevice(root.selectedMetric).connectionName || "—")},
                                                    {label:"Uso do link (estimado)", value:Number(root.currentDevice(root.selectedMetric).percent) >= 0 ? Number(root.currentDevice(root.selectedMetric).percent).toFixed(1) + "%" : "— (velocidade não disponível)"},
                                                    {label:"IPv4", value:String(root.currentDevice(root.selectedMetric).ipv4 || "—")},
                                                    {label:"IPv6", value:String(root.currentDevice(root.selectedMetric).ipv6 || "—")},
                                                    {label:"Gateway", value:String(root.currentDevice(root.selectedMetric).gateway || "—")},
                                                    {label:"MAC", value:String(root.currentDevice(root.selectedMetric).mac || "—")},
                                                    {label:"Velocidade do link", value:String(root.currentDevice(root.selectedMetric).linkSpeed || "—")},
                                                    {label:"Estado", value:String(root.currentDevice(root.selectedMetric).state || "—")},
                                                    {label:"MTU", value:String(root.currentDevice(root.selectedMetric).mtu || "—")}
                                                ] : [
                                                    {label:"Uso GPU total", value:root.metricOverview(root.selectedMetric)},
                                                    {label:"VRAM dedicada", value:Number(root.currentDevice(root.selectedMetric).memoryUsedMiB) >= 0 ? Number(root.currentDevice(root.selectedMetric).memoryUsedMiB).toFixed(0) + " / " + Number(root.currentDevice(root.selectedMetric).memoryTotalMiB).toFixed(0) + " MiB" : "—"},
                                                    {label:"Temperatura", value:Number(root.currentDevice(root.selectedMetric).temperature) >= 0 ? root.currentDevice(root.selectedMetric).temperature + " °C" : "—"},
                                                    {label:"Driver", value:String(root.currentDevice(root.selectedMetric).driver || "—")},
                                                    {label:"PCI", value:String(root.currentDevice(root.selectedMetric).pci || "—")},
                                                    {label:"Link PCIe", value:String(root.currentDevice(root.selectedMetric).pcieLink || "—")},
                                                    {label:"Vulkan (esta GPU)", value:String(root.currentDevice(root.selectedMetric).vulkanVersion || "— (vulkaninfo não identificou GPU)")},
                                                    {label:"OpenGL (contexto padrão)", value:monitor.openGlInfo}
                                                ]
                                                delegate: ColumnLayout {
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    spacing: 4
                                                    Text { font.family: systemFontFamily; text: modelData.label; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                                    Text { font.family: systemFontFamily; text: modelData.value; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (17/13)); font.weight: Font.DemiBold; elide: Text.ElideRight }
                                                }
                                            }
                                        }
                                        Button {
                                            id: logicalButton
                                            visible: root.selectedMetric === "cpu"
                                            text: root.showLogicalProcessors ? "▾  Ocultar processadores lógicos" : "▸  Mostrar processadores lógicos"
                                            onClicked: root.showLogicalProcessors = !root.showLogicalProcessors
                                            contentItem: Text { font.family: systemFontFamily; text: logicalButton.text; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                            background: Rectangle { color: root.inputColor; radius: 9; border.color: root.borderColor }
                                        }
                                        GridLayout {
                                            visible: root.selectedMetric === "cpu" && root.showLogicalProcessors
                                            Layout.fillWidth: true
                                            columns: 6
                                            rowSpacing: 8
                                            columnSpacing: 8
                                            Repeater {
                                                model: monitor.perCoreHistories
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    required property int index
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 90
                                                    color: root.panelColor
                                                    border.color: root.borderColor
                                                    Text { font.family: systemFontFamily; text: "CPU " + index; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (10/13)); anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 6 }
                                                    HistoryGraph {
                                                        anchors.fill: parent
                                                        anchors.margins: 5
                                                        anchors.topMargin: 22
                                                        samples: modelData
                                                        graphColor: root.accentColor
                                                        fillColor: "#204c96bc"
                                                        gridColor: root.borderColor
                                                    }
                                                }
                                            }
                                        }
                                        Text { font.family: systemFontFamily;
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: "Discos: atividade física por /sys/block. Rede: interfaces e IPs via QtNetwork. GPU: DRM/PCI (AMD/Intel) e nvidia-smi. 3D/Copy/Encode/Decode só têm porcentagem quando exposta pelo driver; OpenGL/Vulkan requerem ferramentas de diagnóstico. Sem dados = indisponível."
                                            color: root.secondaryColor
                                            font.pixelSize: Math.round(root.fontUnit * (11/13))
                                        }
                                    }
                                }
                            }
                        }

                        // Inicialização nativa: XDG autostart + scripts reais do KDE Plasma.
                        Item {
                            id: startupPage
                            anchors.fill: parent
                            visible: root.currentPage === "Aplicativos de inicialização"
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 12
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { font.family: systemFontFamily;
                                        Layout.fillWidth: true
                                        text: startupModel.totalCount + " entradas · " + startupModel.enabledCount +
                                              " aplicativos habilitados · " + startupModel.scriptsCount + " scripts"
                                        color: root.secondaryColor
                                        font.pixelSize: Math.round(root.fontUnit * (12/13))
                                        elide: Text.ElideRight
                                    }
                                    Button {
                                        text: "+ Adicionar aplicativo"
                                        onClicked: addStartupDialog.open()
                                        background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 9 }
                                    }
                                    Button {
                                        text: "Importar .desktop"
                                        onClicked: desktopImportDialog.open()
                                        background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 9 }
                                    }
                                    Button {
                                        text: "Abrir KDE"
                                        onClicked: startupModel.openKdeSettings()
                                        background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 9 }
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 43
                                    color: root.panelColor
                                    border.color: root.borderColor
                                    radius: 8
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 14
                                        anchors.rightMargin: 10
                                        spacing: 10
                                        Text { font.family: systemFontFamily; Layout.fillWidth: true; text: "Nome"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                        Text { font.family: systemFontFamily; Layout.preferredWidth: 112; text: "Origem"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                        Text { font.family: systemFontFamily; Layout.preferredWidth: 120; text: "Status"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                        Text { font.family: systemFontFamily; Layout.preferredWidth: 102; text: "Impacto"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                        Text { font.family: systemFontFamily; Layout.preferredWidth: 125; text: "Ações"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                    }
                                }
                                ListView {
                                    id: startupList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: startupModel
                                    boundsBehavior: Flickable.StopAtBounds
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    delegate: Rectangle {
                                        id: startupRow
                                        width: startupList.width
                                        height: rowHeader ? 42 : 57
                                        property bool rowHeader: model.isHeader === true
                                        property string itemId: rowHeader ? "" : String(model.entryId || "")
                                        property string itemName: rowHeader ? "" : String(model.entryName || "")
                                        property string itemStatus: rowHeader ? "" : String(model.entryStatus || "")
                                        property string itemSource: rowHeader ? "" : String(model.entrySource || "")
                                        property string itemNotes: rowHeader ? "" : String(model.entryNotes || "")
                                        property string itemCommand: rowHeader ? "" : String(model.entryCommand || "")
                                        property bool itemEnabled: !rowHeader && model.entryEnabled === true
                                        property bool itemEditable: !rowHeader && model.entryEditable === true
                                        property bool itemRemovable: !rowHeader && model.entryRemovable === true
                                        color: rowHeader ? root.backgroundColor :
                                               (index % 2 ? root.panelColor : root.backgroundColor)
                                        Text { font.family: systemFontFamily;
                                            visible: startupRow.rowHeader
                                            anchors.left: parent.left
                                            anchors.leftMargin: 12
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: model.entryName || ""
                                            color: root.textColor
                                            font.pixelSize: Math.round(root.fontUnit * (15/13))
                                            font.weight: Font.DemiBold
                                        }
                                        RowLayout {
                                            visible: !startupRow.rowHeader
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 10
                                            spacing: 10
                                            CheckBox {
                                                Layout.preferredWidth: 30
                                                visible: startupRow.itemEditable
                                                enabled: startupRow.itemEditable
                                                checked: startupRow.itemEnabled
                                                onClicked: startupModel.setEnabled(startupRow.itemId, checked)
                                                ToolTip.visible: hovered
                                                ToolTip.text: "Habilitar ou desabilitar na próxima sessão do Plasma"
                                            }
                                            Item { Layout.preferredWidth: 30; visible: !startupRow.itemEditable }
                                            Image {
                                                Layout.preferredWidth: 24
                                                Layout.preferredHeight: 24
                                                source: model.entryIcon || ""
                                                sourceSize: Qt.size(48, 48)
                                                fillMode: Image.PreserveAspectFit
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text { font.family: systemFontFamily;
                                                    Layout.fillWidth: true
                                                    text: startupRow.itemName
                                                    color: root.textColor
                                                    font.pixelSize: Math.round(root.fontUnit * (12/13))
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                Text { font.family: systemFontFamily;
                                                    Layout.fillWidth: true
                                                    text: startupRow.itemCommand
                                                    color: root.secondaryColor
                                                    font.pixelSize: Math.round(root.fontUnit * (10/13))
                                                    elide: Text.ElideMiddle
                                                }
                                            }
                                            Text { font.family: systemFontFamily;
                                                Layout.preferredWidth: 112
                                                text: startupRow.itemSource
                                                color: root.secondaryColor
                                                font.pixelSize: Math.round(root.fontUnit * (11/13))
                                                elide: Text.ElideRight
                                            }
                                            Text { font.family: systemFontFamily;
                                                Layout.preferredWidth: 120
                                                text: startupRow.itemStatus
                                                color: startupRow.itemEnabled ? root.textColor : root.secondaryColor
                                                font.pixelSize: Math.round(root.fontUnit * (11/13))
                                                elide: Text.ElideRight
                                            }
                                            Text { font.family: systemFontFamily;
                                                Layout.preferredWidth: 102
                                                text: "Não medido"
                                                color: root.secondaryColor
                                                font.pixelSize: Math.round(root.fontUnit * (11/13))
                                            }
                                            RowLayout {
                                                Layout.preferredWidth: 125
                                                spacing: 6
                                                Button {
                                                    text: "📂"
                                                    onClicked: startupModel.openLocation(startupRow.itemId)
                                                    background: Rectangle { radius: 8; color: parent.hovered ? root.selectedColor : root.inputColor }
                                                    ToolTip.visible: hovered
                                                    ToolTip.text: "Abrir pasta da entrada"
                                                }
                                                Button {
                                                    text: "✕"
                                                    enabled: startupRow.itemRemovable
                                                    onClicked: {
                                                        root.selectedStartupId = startupRow.itemId
                                                        root.selectedStartupName = startupRow.itemName
                                                        removeStartupDialog.open()
                                                    }
                                                    background: Rectangle { radius: 8; color: parent.hovered ? root.selectedColor : root.inputColor }
                                                    ToolTip.visible: hovered
                                                    ToolTip.text: startupRow.itemRemovable ? "Remover entrada do usuário" :
                                                                  "Entrada de sistema ou script: gerencie pelo KDE"
                                                }
                                            }
                                        }
                                        HoverHandler { id: startupHover; enabled: !startupRow.rowHeader }
                                        ToolTip.visible: startupHover.hovered && !startupRow.rowHeader
                                        ToolTip.delay: 750
                                        ToolTip.text: startupRow.itemName + "\n" + model.entryPath +
                                                      (startupRow.itemNotes ? "\n" + startupRow.itemNotes : "")
                                    }
                                }
                                Text { font.family: systemFontFamily;
                                    Layout.fillWidth: true
                                    text: "Leitura real de XDG autostart e scripts do Plasma. Mudanças na inicialização só afetam a próxima sessão. " +
                                          "Impacto não medido; scripts especiais são gerenciados pelo KDE."
                                    color: root.secondaryColor
                                    font.pixelSize: Math.round(root.fontUnit * (10/13))
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        // Página própria: unidades systemd de sistema e usuário.
                        Item {
                            id: servicesPage
                            anchors.fill: parent
                            visible: root.currentPage === "Serviços"
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 7
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { font.family: systemFontFamily;
                                        Layout.fillWidth: true
                                        text: "Unidades systemd · " + servicesModel.totalCount + " serviços · " + servicesModel.runningCount + " ativos"
                                        color: root.secondaryColor
                                        font.pixelSize: Math.round(root.fontUnit * (12/13))
                                    }
                                    Button { text: "↻ Atualizar"; onClicked: servicesModel.refresh() }
                                    Button {
                                        text: "Iniciar"
                                        enabled: root.selectedServiceName !== ""
                                        onClicked: { root.pendingServiceAction = "start"; serviceActionDialog.open() }
                                    }
                                    Button {
                                        text: "Parar"
                                        enabled: root.selectedServiceName !== ""
                                        onClicked: { root.pendingServiceAction = "stop"; serviceActionDialog.open() }
                                    }
                                    Button {
                                        text: "Reiniciar"
                                        enabled: root.selectedServiceName !== ""
                                        onClicked: { root.pendingServiceAction = "restart"; serviceActionDialog.open() }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    TextField {
                                        Layout.fillWidth: true
                                        placeholderText: "Pesquisar unidade ou descrição"
                                        color: root.textColor
                                        placeholderTextColor: root.secondaryColor
                                        background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 9 }
                                        onTextChanged: servicesModel.filterText = text
                                    }
                                    ComboBox {
                                        Layout.preferredWidth: 175
                                        model: ["Sistema + usuário", "Sistema", "Usuário"]
                                        onActivated: function(i) { servicesModel.scopeFilter = ["all", "system", "user"][i] }
                                    }
                                    ComboBox {
                                        Layout.preferredWidth: 155
                                        model: ["Todos os estados", "Em execução", "Inativos/falha"]
                                        onActivated: function(i) { servicesModel.stateFilter = ["all", "active", "inactive"][i] }
                                    }
                                }
                                Text { font.family: systemFontFamily;
                                    visible: servicesModel.lastError !== ""
                                    Layout.fillWidth: true
                                    text: "Aviso: " + servicesModel.lastError
                                    color: root.secondaryColor
                                    font.pixelSize: Math.round(root.fontUnit * (11/13))
                                    elide: Text.ElideRight
                                    ToolTip.visible: errorHover.hovered
                                    ToolTip.text: servicesModel.lastError
                                    HoverHandler { id: errorHover }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 45
                                    color: root.panelColor
                                    border.color: root.borderColor
                                    Row {
                                        anchors.fill: parent
                                        Text { font.family: systemFontFamily; width: parent.width * 0.25; height: parent.height; text: "  Nome"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.34; height: parent.height; text: "Descrição"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.14; height: parent.height; text: "Status"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.13; height: parent.height; text: "Subestado"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.08; height: parent.height; text: "Origem"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.06; height: parent.height; text: "PID"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                    }
                                }
                                ListView {
                                    id: servicesList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: servicesModel
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    delegate: Rectangle {
                                        id: serviceRow
                                        width: servicesList.width
                                        height: 48
                                        property string nameText: model.unitName || ""
                                        property string descriptionText: model.description || ""
                                        property string scopeText: model.scope || ""
                                        property string activeText: model.statusText || ""
                                        property string subText: model.subState || ""
                                        property string pidText: model.mainPidText || "—"
                                        color: root.selectedServiceName === nameText && root.selectedServiceScope === scopeText ?
                                               root.selectedColor : (index % 2 ? root.backgroundColor : root.panelColor)
                                        function chooseService() {
                                            root.selectedServiceName = nameText
                                            root.selectedServiceScope = scopeText
                                            servicesModel.inspectService(nameText, scopeText)
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: function(mouse) {
                                                serviceRow.chooseService()
                                                if (mouse.button === Qt.RightButton) unitMenu.popup()
                                            }
                                        }
                                        Row {
                                            anchors.fill: parent
                                            Text { font.family: systemFontFamily; width: parent.width * 0.25; height: parent.height; text: "  ⚙  " + serviceRow.nameText; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.34; height: parent.height; text: serviceRow.descriptionText; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.14; height: parent.height; text: serviceRow.activeText; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.13; height: parent.height; text: serviceRow.subText; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.08; height: parent.height; text: serviceRow.scopeText === "user" ? "Usuário" : "Sistema"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.06; height: parent.height; text: serviceRow.pidText; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                        }
                                        HoverHandler { id: hoverService }
                                        ToolTip.visible: hoverService.hovered
                                        ToolTip.delay: 850
                                        ToolTip.text: serviceRow.nameText + "\n" + serviceRow.descriptionText + "\n" + serviceRow.activeText
                                        Menu {
                                            id: unitMenu
                                            MenuItem { text: "Iniciar"; onTriggered: { root.pendingServiceAction = "start"; serviceActionDialog.open() } }
                                            MenuItem { text: "Parar"; onTriggered: { root.pendingServiceAction = "stop"; serviceActionDialog.open() } }
                                            MenuItem { text: "Reiniciar"; onTriggered: { root.pendingServiceAction = "restart"; serviceActionDialog.open() } }
                                        }
                                    }
                                }
                                Text { font.family: systemFontFamily;
                                    Layout.fillWidth: true
                                    text: servicesModel.busy ? "Consultando systemd…" :
                                          !servicesModel.available ? "systemd não respondeu neste contexto. Verifique o barramento D-Bus / loginctl." :
                                          "Selecione um serviço para consultar seu PID. Operações usam as permissões da sua sessão (sem sudo automático)."
                                    color: root.secondaryColor
                                    font.pixelSize: Math.round(root.fontUnit * (11/13))
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        // Página própria: contas NSS/Linux, grupos, processos e sessões logind.
                        Item {
                            id: usersPage
                            anchors.fill: parent
                            visible: root.currentPage === "Usuários"
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 9
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { font.family: systemFontFamily;
                                        Layout.fillWidth: true
                                        text: "Usuários Linux · " + usersModel.totalCount + " contas exibidas"
                                        color: root.secondaryColor
                                        font.pixelSize: Math.round(root.fontUnit * (12/13))
                                    }
                                    CheckBox {
                                        text: "Incluir contas do sistema"
                                        checked: usersModel.showSystemUsers
                                        onToggled: usersModel.showSystemUsers = checked
                                    }
                                    Button { text: "↻ Atualizar"; onClicked: usersModel.refresh() }
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    placeholderText: "Pesquisar usuário, UID ou nome completo"
                                    color: root.textColor
                                    placeholderTextColor: root.secondaryColor
                                    background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 9 }
                                    onTextChanged: usersModel.filterText = text
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 44
                                    color: root.panelColor
                                    border.color: root.borderColor
                                    Row {
                                        anchors.fill: parent
                                        Text { font.family: systemFontFamily; width: parent.width * 0.11; height: parent.height; text: "  Usuário"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.06; height: parent.height; text: "UID"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.13; height: parent.height; text: "Nome completo"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.15; height: parent.height; text: "Diretório pessoal"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.12; height: parent.height; text: "Shell"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.19; height: parent.height; text: "Grupos"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.07; height: parent.height; text: "Procs."; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.08; height: parent.height; text: "RAM"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                        Text { font.family: systemFontFamily; width: parent.width * 0.09; height: parent.height; text: "Sessão"; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter }
                                    }
                                }
                                ListView {
                                    id: usersList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: usersModel
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    delegate: Rectangle {
                                        id: userRow
                                        width: usersList.width
                                        height: 62
                                        property string userValue: model.userName || ""
                                        property string uidValue: String(model.uidValue === undefined ? "" : model.uidValue)
                                        property string nameValue: model.fullName || ""
                                        property string homeValue: model.homeDir || ""
                                        property string shellValue: model.shellPath || ""
                                        property string groupValue: model.groupsText || ""
                                        property string countValue: String(model.processCount || 0)
                                        property string memoryValue: root.prettyMemory(model.memoryMiB || 0)
                                        property string sessionValue: model.sessionText || "—"
                                        color: index % 2 ? root.backgroundColor : root.panelColor
                                        Row {
                                            anchors.fill: parent
                                            Text { font.family: systemFontFamily; width: parent.width * 0.11; height: parent.height; text: "  " + userRow.userValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.06; height: parent.height; text: userRow.uidValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.13; height: parent.height; text: userRow.nameValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.15; height: parent.height; text: userRow.homeValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.12; height: parent.height; text: userRow.shellValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.19; height: parent.height; text: userRow.groupValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.07; height: parent.height; text: userRow.countValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.08; height: parent.height; text: userRow.memoryValue; color: root.textColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { font.family: systemFontFamily; width: parent.width * 0.09; height: parent.height; text: userRow.sessionValue; color: root.secondaryColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                        }
                                        HoverHandler { id: hoverUser }
                                        ToolTip.visible: hoverUser.hovered
                                        ToolTip.delay: 900
                                        ToolTip.text: userRow.userValue + " (UID " + userRow.uidValue + ")\n" +
                                                      "Grupos: " + userRow.groupValue + "\n" +
                                                      "Home: " + userRow.homeValue + "\n" +
                                                      "Shell: " + userRow.shellValue
                                    }
                                }
                                Text { font.family: systemFontFamily;
                                    Layout.fillWidth: true
                                    text: "Fontes: NSS/getpwent, grupos Linux, /proc e logind. RAM somada por RSS (páginas compartilhadas podem ser contadas em duplicidade)."
                                    color: root.secondaryColor
                                    font.pixelSize: Math.round(root.fontUnit * (11/13))
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Item {
                            anchors.fill: parent
                            visible: root.currentPage !== "Processos" && root.currentPage !== "Detalhes" &&
                                     root.currentPage !== "Desempenho" && root.currentPage !== "Configurações" &&
                                     root.currentPage !== "Usuários" && root.currentPage !== "Serviços" &&
                                     root.currentPage !== "Aplicativos de inicialização"
                            ColumnLayout {
                                anchors.centerIn: parent
                                width: Math.min(520, parent.width - 60)
                                spacing: 14
                                Text { font.family: systemFontFamily; Layout.alignment: Qt.AlignHCenter; text: "◈"; font.pixelSize: Math.round(root.fontUnit * (46/13)); color: root.accentColor }
                                Text { font.family: systemFontFamily; Layout.alignment: Qt.AlignHCenter; text: root.currentPage; font.pixelSize: Math.round(root.fontUnit * (21/13)); color: root.textColor; font.weight: Font.DemiBold }
                                Text { font.family: systemFontFamily;
                                    Layout.fillWidth: true
                                    text: "Esta página está preparada na interface, mas a implementação nativa ainda não foi concluída. Não são exibidos dados fictícios."
                                    color: root.secondaryColor
                                    font.pixelSize: Math.round(root.fontUnit * (14/13))
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }
                                Button {
                                    Layout.alignment: Qt.AlignHCenter
                                    id: openProcessButton
                                    text: "Abrir processos"
                                    onClicked: root.currentPage = "Processos"
                                    contentItem: Text { font.family: systemFontFamily; text: openProcessButton.text; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (13/13)) }
                                    background: Rectangle { radius: 7; color: root.inputColor; border.color: root.borderColor }
                                }
                            }
                        }

                        Item {
                            anchors.fill: parent
                            visible: root.currentPage === "Configurações"
                            ColumnLayout {
                                anchors.left: parent.left; anchors.top: parent.top
                                width: Math.min(620, parent.width)
                                spacing: 15
                                Text { font.family: systemFontFamily; text: "Aparência"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (18/13)); font.weight: Font.DemiBold }
                                CheckBox { text: "Seguir cores e tema do KDE Plasma"; checked: root.useKdePalette; onToggled: root.useKdePalette = checked; palette.windowText: root.textColor }
                                CheckBox { text: "Tema escuro manual"; enabled: !root.useKdePalette; checked: root.darkMode; onToggled: root.darkMode = checked; palette.windowText: root.textColor }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.borderColor }
                                Text { font.family: systemFontFamily; text: "Gerenciamento de janelas"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (18/13)); font.weight: Font.DemiBold }
                                CheckBox {
                                    id: alwaysOnTopCheckBox
                                    text: "Sempre acima das outras janelas"
                                    checked: root.alwaysOnTop
                                    onToggled: root.alwaysOnTop = checked
                                    palette.windowText: root.textColor
                                }
                                Text {
                                    font.family: systemFontFamily
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: "No Plasma Wayland, o KWin pode ignorar essa solicitação do Qt. Se a janela não permanecer acima, use Alt+F3 → Mais ações → Manter acima das outras janelas, ou crie uma regra de janela no KDE."
                                    color: root.secondaryColor
                                    font.pixelSize: Math.round(root.fontUnit * (11/13))
                                }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.borderColor }
                                Text { font.family: systemFontFamily; text: "Velocidade de atualização"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (18/13)); font.weight: Font.DemiBold }
                                ComboBox {
                                    Layout.preferredWidth: 220
                                    model: ["Rápido (1 segundo)", "Normal (1,5 segundo)", "Lento (3 segundos)", "Muito lento (10 segundos)"]
                                    currentIndex: 1
                                    onActivated: function(index) { monitor.intervalMs = [1000, 1500, 3000, 10000][index] }
                                }
                                Text { font.family: systemFontFamily; text: "Tarefas11 · 0.4.16 para Linux / Wine / Proton · KDE Plasma 6.8"; color: root.secondaryColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
                                Text { font.family: systemFontFamily;
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: "Desenvolvido com C++20, Qt 6 e Qt Quick. Uma aparência parecida com o Windows não significa uso das APIs de processos do Windows; todas as ações são nativas do Linux e respeitam as permissões da sessão."
                                    color: root.secondaryColor
                                    font.pixelSize: Math.round(root.fontUnit * (12/13))
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Resumo compacto: conserva os cartões/minigráficos de Desempenho e
    // dispensa barra superior, navegação e painel de detalhes. Rola a lista
    // inteira quando há mais dispositivos que o espaço da janela.
    ScrollView {
        id: compactPerformanceView
        anchors.fill: parent
        visible: root.compactMode
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        background: Rectangle { color: root.backgroundColor }
        Column {
            width: Math.max(280, compactPerformanceView.availableWidth - 12)
            spacing: 6
            Rectangle {
                width: parent.width
                height: 35
                radius: 6
                color: root.navColor
                Text {
                    anchors.centerIn: parent
                    text: "Arraste para mover · dois cliques para voltar"
                    width: parent.width - 16
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: root.secondaryColor
                    font.family: root.font.family
                    font.pixelSize: Math.round(root.fontUnit * (11/13))
                }
                // Sem MouseArea envolvendo o DragHandler: o compositor pode
                // receber o gesto de mover sem perder o duplo clique.
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.DragThreshold
                    onDoubleTapped: root.toggleCompactMode()
                }
                DragHandler {
                    target: null
                    enabled: root.compactMode
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged: {
                        if (active && root.compactMode) root.startSystemMove()
                    }
                }
            }
            Repeater {
                model: root.performanceMetrics
                delegate: MetricTile {
                    required property var modelData
                    width: parent ? parent.width : 350
                    metricTitle: modelData.label
                    subtitle: root.metricSubtitle(modelData.key)
                    points: root.metricHistory(modelData.key)
                    accentColor: modelData.hue
                    primaryText: root.textColor
                    baseFont: root.fontUnit
                    secondaryText: root.secondaryColor
                    frameColor: root.borderColor
                    selected: root.selectedMetric === modelData.key
                    percentageGraph: modelData.kind !== "net"
                    allowWindowMove: root.compactMode
                    onMoveWindowRequested: {
                        if (root.compactMode) root.startSystemMove()
                    }
                    onClicked: root.selectedMetric = modelData.key
                    onDoubleClicked: root.toggleCompactMode()
                }
            }
        }
    }

    Dialog {
        id: addStartupDialog
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 470
        modal: true
        title: "Adicionar aplicativo à inicialização do KDE"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: startupModel.addApplication(startupNameField.text, startupCommandField.text)
        onOpened: {
            startupNameField.clear()
            startupCommandField.clear()
            startupNameField.forceActiveFocus()
        }
        background: Rectangle { radius: 8; color: root.panelColor; border.color: root.borderColor }
        contentItem: ColumnLayout {
            width: 428
            spacing: 10
            Text { font.family: systemFontFamily; text: "Nome do aplicativo"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
            TextField { id: startupNameField; Layout.fillWidth: true; placeholderText: "Ex.: Meu aplicativo" }
            Text { font.family: systemFontFamily; text: "Comando de inicialização"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (12/13)) }
            TextField { id: startupCommandField; Layout.fillWidth: true; placeholderText: "/usr/bin/programa --opcao" }
            Text { font.family: systemFontFamily;
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "O comando será salvo em ~/.config/autostart como .desktop. Não será executado agora."
                color: root.secondaryColor
                font.pixelSize: Math.round(root.fontUnit * (11/13))
            }
        }
    }
    QuickDialogs.FileDialog {
        id: desktopImportDialog
        title: "Selecionar aplicativo .desktop"
        // Modo padrão do QtQuick.Dialogs.FileDialog: abrir arquivo.
        nameFilters: ["Aplicativos (*.desktop)"]
        onAccepted: startupModel.importDesktopFile(selectedFile)
    }
    Dialog {
        id: removeStartupDialog
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 440
        modal: true
        title: "Remover da inicialização"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: startupModel.removeEntry(root.selectedStartupId)
        background: Rectangle { radius: 8; color: root.panelColor; border.color: root.borderColor }
        contentItem: Text { font.family: systemFontFamily;
            width: 400
            wrapMode: Text.WordWrap
            text: "Remover a entrada de inicialização '" + root.selectedStartupName + "'? " +
                  "O aplicativo instalado não será desinstalado."
            color: root.textColor
        }
    }

    Dialog {
        id: serviceActionDialog
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 460
        modal: true
        title: "Confirmar ação no systemd"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: servicesModel.changeService(root.selectedServiceName, root.selectedServiceScope, root.pendingServiceAction)
        background: Rectangle { radius: 8; color: root.panelColor; border.color: root.borderColor }
        contentItem: Text { font.family: systemFontFamily;
            width: 415
            wrapMode: Text.WordWrap
            text: "Executar '" + root.pendingServiceAction + "' no serviço " + root.selectedServiceName +
                  " (" + root.selectedServiceScope + ")? Serviços do sistema podem exigir autorização administrativa."
            color: root.textColor
        }
    }
    Dialog {
        id: endTaskDialog
        property bool force: false
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 430
        modal: true
        title: force ? "Forçar encerramento" : "Finalizar tarefa"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: processes.terminate(root.selectedPid, root.selectedStartTime, force)
        background: Rectangle { radius: 10; color: root.panelColor; border.color: root.borderColor }
        contentItem: Text { font.family: systemFontFamily;
            text: endTaskDialog.force
                ? "Enviar SIGKILL ao processo PID " + root.selectedPid + "? Ele não poderá finalizar normalmente."
                : "Enviar SIGTERM ao processo PID " + root.selectedPid + "? Os dados não salvos podem ser perdidos."
            width: 390
            wrapMode: Text.WordWrap
            color: root.textColor
            font.pixelSize: Math.round(root.fontUnit * (14/13))
        }
    }
    Dialog {
        id: efficiencyDialog
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 455
        modal: true
        title: "Reduzir prioridade de CPU"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: processes.reducePriority(root.selectedPid, root.selectedStartTime)
        background: Rectangle { radius: 10; color: root.panelColor; border.color: root.borderColor }
        contentItem: Text { font.family: systemFontFamily;
            text: "Definir nice=10 para o PID " + root.selectedPid + "? Isso não é o modo EcoQoS do Windows. Pode ser impossível restaurar a prioridade normal sem privilégios de administrador ou reiniciar o processo."
            width: 402
            wrapMode: Text.WordWrap
            color: root.textColor
            font.pixelSize: Math.round(root.fontUnit * (14/13))
        }
    }
    Dialog {
        id: launchDialog
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 440
        modal: true
        title: "Executar nova tarefa"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: processes.launchCommand(commandInput.text)
        onOpened: { commandInput.text = ""; commandInput.forceActiveFocus() }
        background: Rectangle { radius: 10; color: root.panelColor; border.color: root.borderColor }
        contentItem: ColumnLayout {
            spacing: 8
            Text { font.family: systemFontFamily; text: "Digite o executável e os argumentos (sem shell):"; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (13/13)) }
            TextField {
                id: commandInput
                Layout.fillWidth: true
                placeholderText: "Ex.: konsole ou firefox --private-window"
                color: root.textColor
                background: Rectangle { color: root.inputColor; border.color: root.borderColor; radius: 9 }
                onAccepted: launchDialog.accept()
            }
        }
    }
    Popup {
        id: notification
        parent: Overlay.overlay
        x: (root.width - width) / 2
        y: root.height - height - 32
        width: Math.min(root.width - 40, 530)
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: root.panelColor; radius: 9; border.color: root.accentColor }
        contentItem: Text { font.family: systemFontFamily; text: root.alertMessage; color: root.textColor; font.pixelSize: Math.round(root.fontUnit * (13/13)); wrapMode: Text.WordWrap }
    }
    Timer { id: notificationTimer; interval: 4200; onTriggered: notification.close() }
}
