import QtQuick

Item {
    id: root
    property var samples: []
    property color graphColor: "#64b5f6"
    property color fillColor: "#2664b5f6"
    property color gridColor: "#293643"
    property real maxValue: 100
    property bool showGrid: true
    property real inset: 2

    onSamplesChanged: surface.requestPaint()
    onGraphColorChanged: surface.requestPaint()
    onFillColorChanged: surface.requestPaint()
    onGridColorChanged: surface.requestPaint()
    onMaxValueChanged: surface.requestPaint()
    onShowGridChanged: surface.requestPaint()
    onWidthChanged: surface.requestPaint()
    onHeightChanged: surface.requestPaint()

    Canvas {
        id: surface
        anchors.fill: parent
        renderTarget: Canvas.Image
        onPaint: {
            const ctx = getContext("2d")
            const w = width, h = height
            ctx.clearRect(0, 0, w, h)
            if (w <= 0 || h <= 0) return

            if (root.showGrid) {
                ctx.beginPath()
                ctx.strokeStyle = root.gridColor
                ctx.lineWidth = 1
                for (let x = 0; x <= 6; ++x) {
                    const px = Math.round(x * w / 6) + 0.5
                    ctx.moveTo(px, 0)
                    ctx.lineTo(px, h)
                }
                for (let y = 0; y <= 4; ++y) {
                    const py = Math.round(y * h / 4) + 0.5
                    ctx.moveTo(0, py)
                    ctx.lineTo(w, py)
                }
                ctx.stroke()
            }

            const n = root.samples ? root.samples.length : 0
            if (n === 0) return
            const ceiling = Math.max(0.0001, root.maxValue)
            const bottom = Math.max(1, h - root.inset)
            const xFor = (i) => n <= 1 ? w : (i * w / (n - 1))
            const yFor = (value) => bottom - Math.min(1, Math.max(0, Number(value) / ceiling)) * (h - root.inset * 2)

            ctx.beginPath()
            ctx.moveTo(0, bottom)
            for (let i = 0; i < n; ++i) ctx.lineTo(xFor(i), yFor(root.samples[i]))
            ctx.lineTo(w, bottom)
            ctx.closePath()
            ctx.fillStyle = root.fillColor
            ctx.fill()

            ctx.beginPath()
            for (let i = 0; i < n; ++i) {
                if (i === 0) ctx.moveTo(xFor(i), yFor(root.samples[i]))
                else ctx.lineTo(xFor(i), yFor(root.samples[i]))
            }
            ctx.strokeStyle = root.graphColor
            ctx.lineWidth = 1.8
            ctx.lineJoin = "round"
            ctx.stroke()
        }
    }
}
