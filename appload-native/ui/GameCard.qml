import QtQuick
import QtQuick.Layouts

// One game as a card: status, both teams with record and score, and for a game
// in progress the bases and outs. Outline only — a filled card would be a big
// dark area, which e-ink repaints slowly.
Item {
    id: card

    property color ink
    property color faint
    property color muted
    // Losers recede, but still have to be legible. Kept separate from `muted` so
    // making secondary text darker does not silently flatten the winner/loser
    // distinction. #8A8A8A reads fine on a monitor and is not readable on
    // Gallery 3; #4A4A4A is.
    property color dimmed: "#4A4A4A"
    property color accent
    property var   game: ({})
    property var   logoSource: function (id) { return "" }
    property real  u: 1.0            // scale for everything inside

    // Column widths are a share of the card, not fixed pixels. With fixed
    // widths a two-column portrait grid made the row wider than the card, so
    // the bases were pushed past the edge and clipped away entirely.
    readonly property real logoW:    Math.round(width * 0.115)
    readonly property real abbrW:    Math.round(width * 0.22)
    readonly property real scoreW:   Math.round(width * 0.13)
    readonly property real diamondW: Math.round(width * 0.175)

    signal picked()

    implicitHeight: frame.implicitHeight

    readonly property bool live:  game.isLive === true
    readonly property bool over:  game.isFinal === true
    // Once a game is over, the loser drops back to grey.
    function dim(mine, theirs) {
        return card.over && mine < theirs
    }

    // Narrow cards were letting the diamond and outs spill past the border.
    clip: true

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: 16 * card.u
        color: "transparent"
        border.width: Math.max(1, 2 * card.u)
        border.color: card.faint

        implicitHeight: body.implicitHeight + 2 * (26 * card.u)

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 26 * card.u
            spacing: 12 * card.u

            Text {
                text: card.game.note !== undefined ? card.game.note : ""
                color: card.live ? card.accent : card.muted
                font.pixelSize: 33 * card.u
                font.bold: true
                font.letterSpacing: 3 * card.u
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14 * card.u

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * card.u

                    Repeater {
                        model: [
                            { id: card.game.awayId,   abbr: card.game.awayAbbr,
                              rec: card.game.awayRecord, runs: card.game.awayRuns,
                              lost: card.dim(card.game.awayRuns, card.game.homeRuns) },
                            { id: card.game.homeId,   abbr: card.game.homeAbbr,
                              rec: card.game.homeRecord, runs: card.game.homeRuns,
                              lost: card.dim(card.game.homeRuns, card.game.awayRuns) }
                        ]

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12 * card.u

                            Image {
                                Layout.preferredWidth: card.logoW
                                Layout.preferredHeight: card.logoW
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                opacity: modelData.lost ? 0.45 : 1.0
                                source: card.logoSource(modelData.id)
                                visible: source != ""
                            }
                            Text {
                                text: modelData.abbr !== undefined ? modelData.abbr : ""
                                color: modelData.lost ? card.dimmed : card.ink
                                font.pixelSize: 56 * card.u
                                font.bold: true
                                Layout.preferredWidth: card.abbrW
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: modelData.rec !== undefined ? modelData.rec : ""
                                color: card.muted
                                font.pixelSize: 32 * card.u
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: (modelData.runs === undefined || modelData.runs === "")
                                      ? "–" : modelData.runs
                                color: modelData.lost ? card.dimmed : card.ink
                                font.pixelSize: 56 * card.u
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: card.scoreW
                            }
                        }
                    }
                }

                // Bases and outs, only while a game is actually being played.
                ColumnLayout {
                    visible: card.live
                    spacing: 12 * card.u
                    Layout.alignment: Qt.AlignVCenter

                    BaseDiamond {
                        Layout.preferredWidth: card.diamondW
                        Layout.preferredHeight: card.diamondW
                        ink: card.ink
                        faint: card.faint
                        first:  card.game.onFirst === true
                        second: card.game.onSecond === true
                        third:  card.game.onThird === true
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 7 * card.u
                        Repeater {
                            model: 3
                            Rectangle {
                                width: 18 * card.u
                                height: width
                                radius: width / 2
                                color: index < (card.game.outs || 0) ? card.ink : "transparent"
                                border.width: Math.max(1, 2 * card.u)
                                border.color: card.faint
                            }
                        }
                    }
                }
            }
        }
    }

    TapHandler { onTapped: card.picked() }
}
