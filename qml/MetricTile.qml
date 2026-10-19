import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    property real baseFont: systemFontSize
    property string metricTitle: "CPU"
    property string subtitle: ""
    property var points: []
    property color accentColor: "#63b1ee"
    property color primaryText: "#e7ecf3"
    property color secondaryText: "#abb9c7"
    property color frameColor: "#344251"
    property bool selected: false
    property bool percentageGraph: true
    // Normalmente os cartões nunca movem a janela. Só o modo mínimo ativa.
    property bool allowWindowMove: false
    signal clicked()
    signal doubleClicked()
    signal moveWindowRequested()
    function autoscale() {
        let maximum = 2
        if (root.points) {
            for (let i = 0; i < root.points.length; ++i)
                maximum = Math.max(maximum, Number(root.points[i]))
        }
        return maximum * 1.12
    }

    // O cartão cresce somente quando houver nomes ou descrições longas;
    // ambos permanecem legíveis, mesmo com escalonamento de fontes KDE.
    implicitHeight: Math.max(84, labels.implicitHeight + 20)
    radius: 9
    color: selected ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.15) : "transparent"
    border.color: selected ? accentColor : frameColor
    border.width: selected ? 2 : 1

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 9
        Rectangle {
            Layout.preferredWidth: Math.max(52, Math.min(66, root.width * 0.23))
            Layout.preferredHeight: 45
            radius: 3
            color: "transparent"
            border.color: root.frameColor
            HistoryGraph {
                anchors.fill: parent
                anchors.margins: 2
                graphColor: root.accentColor
                fillColor: Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.17)
                gridColor: Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.13)
                showGrid: false
                samples: root.points
                maxValue: root.percentageGraph ? 100 : root.autoscale()
            }
        }
        ColumnLayout {
            id: labels
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.minimumWidth: 0
            spacing: 3
            Text {
                font.family: systemFontFamily
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: 0
                text: root.metricTitle
                font.pixelSize: Math.round(root.baseFont * (15 / 13))
                font.weight: Font.DemiBold
                color: root.primaryText
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
            Text {
                font.family: systemFontFamily
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: 0
                text: root.subtitle
                font.pixelSize: Math.round(root.baseFont * (12 / 13))
                color: root.secondaryText
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }
    }
    // Qt Quick 6.8: TapHandler e DragHandler irmãos do próprio cartão.
    // Na revisão anterior o DragHandler era filho de MouseArea, que capturava
    // o mouse e podia impedir o gesto de arrastar em KWin/Wayland.
    HoverHandler {
        id: tileHover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onSingleTapped: root.clicked()
        onDoubleTapped: root.doubleClicked()
    }
    DragHandler {
        target: null
        enabled: root.allowWindowMove
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active && root.allowWindowMove) root.moveWindowRequested()
        }
    }
    ToolTip.visible: tileHover.hovered
    ToolTip.delay: 650
    ToolTip.text: root.metricTitle + "\n" + root.subtitle
}
