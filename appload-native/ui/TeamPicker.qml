import QtQuick
import QtQuick.Layouts

// Choose which club the board follows.
//
// Picking a team takes effect everywhere immediately -- except the launcher
// icon, which AppLoad only reads when xochitl starts. That is stated plainly
// at the top rather than left as a surprise.
Item {
    id: picker

    property color ink
    property color faint
    property color muted
    property color accent
    property var   teams: []
    property int   currentId: 0
    property real  u: 1.0
    property var   logoSource: function (id) { return "" }

    signal teamChosen(int teamId)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Text {
            text: "FOLLOW A TEAM"
            color: picker.muted
            font.pixelSize: 34 * picker.u
            font.bold: true
            font.letterSpacing: 5 * picker.u
        }

        Item { Layout.preferredHeight: 8 * picker.u }

        Text {
            Layout.fillWidth: true
            text: "The board changes straight away. The launcher icon follows "
                  + "the next time your tablet restarts."
            // muted, not faint: faint is for rules and outlines. Secondary text
            // set in it is close to invisible on Gallery 3.
            color: picker.muted
            font.pixelSize: 26 * picker.u
            wrapMode: Text.WordWrap
        }

        Item { Layout.preferredHeight: 20 * picker.u }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 2; color: picker.faint }

        // Thirty clubs: three columns fit without scrolling, which keeps this
        // to a single e-ink repaint.
        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 16 * picker.u
            columns: picker.width > picker.height ? 4 : 3
            columnSpacing: 18 * picker.u
            rowSpacing: 14 * picker.u

            Repeater {
                model: picker.teams

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 84 * picker.u
                    radius: 10 * picker.u
                    color: "transparent"
                    border.width: modelData.id === picker.currentId
                                  ? Math.max(2, 4 * picker.u)
                                  : Math.max(1, 2 * picker.u)
                    border.color: modelData.id === picker.currentId
                                  ? picker.accent : picker.faint

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12 * picker.u
                        spacing: 10 * picker.u

                        Image {
                            Layout.preferredWidth: 46 * picker.u
                            Layout.preferredHeight: 46 * picker.u
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            source: picker.logoSource(modelData.id)
                            visible: source != ""
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.abbr
                            color: picker.ink
                            font.pixelSize: 34 * picker.u
                            font.bold: modelData.id === picker.currentId
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: picker.teamChosen(modelData.id)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
