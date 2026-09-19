import QtQuick
import QtQuick.Layouts

// Today's games as a grid of cards. Column count follows the available width,
// so portrait gets two and landscape four without either being hard-coded.
Item {
    id: slate

    property color ink
    property color faint
    property color muted
    property color accent
    property var games: []
    property string dateLabel: ""
    property bool loaded: false
    property bool headerVisible: true
    property var logoSource: function (id) { return "" }
    property real u: 1.0

    readonly property real gap: 26 * slate.u
    // Fitting as many as physically possible made every card unreadable at
    // arm's length. Pick a count that keeps them big: three across in
    // landscape, two in portrait.
    readonly property int cols: width > height ? 3 : 2

    signal gamePicked(var row)
    signal dismissed()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Text {
            text: {
                var head = slate.dateLabel.length > 0 ? slate.dateLabel : "TODAY"
                if (!slate.loaded)
                    return "LOADING…"
                return head + " — " + (slate.games.length > 0
                                       ? slate.games.length + " GAMES"
                                       : "NO GAMES")
            }
            color: slate.muted
            font.pixelSize: 40 * slate.u
            font.letterSpacing: 5 * slate.u
            font.bold: true
        }

        Item { Layout.preferredHeight: 24 * slate.u }

        // A full slate is fifteen games, which at two columns is eight rows and
        // does not fit on either orientation -- the grid simply ran off the
        // bottom edge with no way to reach the rest.
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: grid.height + filler.height
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            // No coasting. A flick that keeps moving after the finger lifts is
            // a full panel repaint per frame; drag-to-scroll is one repaint per
            // drag and is what this panel can actually keep up with. A very
            // high deceleration stops it dead without relying on
            // maximumFlickVelocity, whose zero case is not well defined.
            flickDeceleration: 100000

            Column {
                width: flick.width

                GridLayout {
                    id: grid
                    width: flick.width
                    columns: slate.cols
                    columnSpacing: slate.gap
                    rowSpacing: slate.gap

                    Repeater {
                        model: slate.games

                        GameCard {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1   // let the grid divide evenly
                            ink: slate.ink
                            faint: slate.faint
                            muted: slate.muted
                            accent: slate.accent
                            logoSource: slate.logoSource
                            u: slate.u
                            game: modelData
                            onPicked: slate.gamePicked(modelData)
                        }
                    }
                }

                // Empty space below the grid backs out without changing game,
                // exactly as it did before. It only exists when the games fit;
                // when they do not, backing out is the top strip's job and this
                // is zero-high, so it never eats a tap meant for a card.
                Item {
                    id: filler
                    width: flick.width
                    height: Math.max(0, flick.height - grid.height)
                    TapHandler { onTapped: slate.dismissed() }
                }
            }
        }

        // Scrollability has to be visible or it does not exist. A moving bar
        // would repaint the panel the whole way down, so this is a static line
        // that simply says there is more, and disappears at the bottom.
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 6 * slate.u
            horizontalAlignment: Text.AlignHCenter
            visible: flick.contentHeight > flick.height + 1
                     && flick.contentY < flick.contentHeight - flick.height - 1
            text: "MORE BELOW"
            color: slate.muted
            font.pixelSize: 22 * slate.u
            font.bold: true
            font.letterSpacing: 4 * slate.u
        }
    }
}
