import QtQuick

// The three bases. A filled base means a runner is standing on it.
// No home plate: it can never be occupied, so it was decoration.
Item {
    id: d

    property bool first: false
    property bool second: false
    property bool third: false
    property color ink: "#000000"
    property color faint: "#9A9A9A"

    // 0.26 keeps the rotated corners clear of each other. Anything much
    // above 0.28 and second base collides with first and third.
    property real cell: Math.min(width, height) * 0.26
    property real stroke: Math.max(4, cell * 0.075)

    component Base: Rectangle {
        property bool occupied: false
        property real cx: 0
        property real cy: 0
        property color line: d.ink

        width: d.cell
        height: d.cell
        x: cx - width / 2
        y: cy - height / 2
        rotation: 45
        antialiasing: true
        color: occupied ? d.ink : "transparent"
        border.color: line
        border.width: d.stroke
    }

    // With home plate gone the old positions left the bases sitting in the top
    // two thirds of the box, so anything centred beside them looked low. These
    // centre the triangle in the box instead.
    readonly property real spread: height * 0.15

    Base { occupied: d.second; cx: d.width / 2;             cy: d.height / 2 - d.spread }
    Base { occupied: d.third;  cx: d.cell * 0.95;           cy: d.height / 2 + d.spread }
    Base { occupied: d.first;  cx: d.width - d.cell * 0.95; cy: d.height / 2 + d.spread }
}
