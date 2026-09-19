import QtQuick

// Small-caps bar with a rule under it.
Item {
    id: strip

    property string leftText: ""
    property string rightText: ""
    property color ink: "#000000"
    property color faint: "#9A9A9A"   // the rule
    property color muted: "#545454"   // the text
    property color accent: "#A4123F"
    property bool accentRight: false

    implicitHeight: 62

    Text {
        anchors.left: parent.left
        anchors.top: parent.top
        text: strip.leftText
        color: strip.ink
        font.pixelSize: 32
        font.letterSpacing: 6
        font.weight: Font.DemiBold
    }

    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        text: strip.rightText
        color: strip.accentRight ? strip.accent : strip.muted
        font.pixelSize: 32
        font.letterSpacing: 6
        font.weight: Font.DemiBold
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 3
        color: strip.faint
    }
}
