import QtQuick

// Classic inning-by-inning grid with R / H / E on the right.
Column {
    id: ls

    property var innings: []
    property string awayAbbr: ""
    property string homeAbbr: ""
    property int awayR: 0
    property int homeR: 0
    property int awayH: 0
    property int homeH: 0
    property int awayE: 0
    property int homeE: 0

    property color ink: "#000000"
    property color faint: "#9A9A9A"   // rules and outlines
    property color muted: "#545454"   // secondary text
    property real teamColWidth: 150
    // Always lay out at least nine innings, blank until played. Sizing the
    // columns off however many innings exist so far made the whole grid reflow
    // every time one finished -- wide cells in the 1st, cramped by the 9th.
    // Extra innings extend it; nothing ever shrinks.
    readonly property var grid: {
        var out = []
        var have = innings ? innings.length : 0
        var n = Math.max(9, have)
        for (var i = 0; i < n; i++) {
            var src = (i < have) ? innings[i] : null
            out.push({
                num:  i + 1,
                away: src ? src.away : "",
                home: src ? src.home : ""
            })
        }
        return out
    }

    property real cellWidth: (width - teamColWidth) / (grid.length + 3)
    property real fontSize: 34

    spacing: 10

    component Cell: Text {
        property bool heavy: false
        width: ls.cellWidth
        color: ls.ink
        font.pixelSize: ls.fontSize
        font.weight: heavy ? Font.Bold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
    }

    // Header
    Row {
        Text {
            width: ls.teamColWidth
            text: ""
            font.pixelSize: ls.fontSize
        }
        Repeater {
            model: ls.grid
            Cell {
                text: modelData.num
                color: ls.muted
                font.weight: Font.DemiBold
            }
        }
        Cell { text: "R"; heavy: true }
        Cell { text: "H"; heavy: true }
        Cell { text: "E"; heavy: true }
    }

    Rectangle { width: ls.width; height: 3; color: ls.faint }

    // Away
    Row {
        Text {
            width: ls.teamColWidth
            text: ls.awayAbbr
            color: ls.ink
            font.pixelSize: ls.fontSize
            font.weight: Font.DemiBold
            font.letterSpacing: 2
        }
        Repeater {
            model: ls.grid
            Cell { text: modelData.away }
        }
        Cell { text: ls.awayR; heavy: true }
        Cell { text: ls.awayH }
        Cell { text: ls.awayE }
    }

    // Home
    Row {
        Text {
            width: ls.teamColWidth
            text: ls.homeAbbr
            color: ls.ink
            font.pixelSize: ls.fontSize
            font.weight: Font.DemiBold
            font.letterSpacing: 2
        }
        Repeater {
            model: ls.grid
            Cell { text: modelData.home }
        }
        Cell { text: ls.homeR; heavy: true }
        Cell { text: ls.homeH }
        Cell { text: ls.homeE }
    }
}
