import QtQuick

// One labelled row of pips, e.g.  STRIKES  ● ● ○
Row {
    id: row

    property string label: ""
    property int value: 0
    property int total: 3
    property color ink: "#000000"
    property color faint: "#9A9A9A"
    property real pip: 42
    property real labelWidth: 215
    property real gap: 22

    spacing: gap

    Text {
        width: row.labelWidth
        text: row.label
        color: row.ink
        font.pixelSize: 34
        font.letterSpacing: 4
        font.weight: Font.DemiBold
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignRight
    }

    Repeater {
        model: row.total
        Rectangle {
            width: row.pip
            height: row.pip
            radius: width / 2
            antialiasing: true
            anchors.verticalCenter: parent.verticalCenter
            color: index < row.value ? row.ink : "transparent"
            border.color: index < row.value ? row.ink : row.faint
            border.width: 4
        }
    }
}
