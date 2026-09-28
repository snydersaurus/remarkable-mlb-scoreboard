import QtQuick
import QtQuick.Layouts
import net.asivery.AppLoad 1.0

// AppLoad frontend. This runs *inside* xochitl, so there is no panel to own, no
// rotation to apply and no framebuffer involved -- xochitl hands us a window and
// we fill it. Everything the view needs arrives as JSON from the backend, which
// is a separate process precisely so it can reach the network.
Item {
    id: root
    anchors.fill: parent

    // AppLoad calls these when the app is being torn down.
    signal close
    function unloading() {
        appload.sendMessage(root.msgShowTeam, "")
    }

    // Diagnostic: report the window we are given, and any change to it, so the
    // backend can log it. Whether AppLoad flips the window on rotation is the
    // whole question and it should be measured, not inferred from its source.
    function reportGeometry() {
        appload.sendMessage(root.msgGeometry,
                            Math.round(width) + "x" + Math.round(height))
    }
    onWidthChanged: reportGeometry()
    onHeightChanged: reportGeometry()

    // Ask for fresh data the moment the app is on screen again, rather than
    // waiting up to a poll interval to notice. AppLoad keeps a frontend loaded
    // when you close the window, so Component.onCompleted does not run a
    // second time and the backend would otherwise hear nothing about you
    // coming back.
    onVisibleChanged: {
        if (visible)
            appload.sendMessage(root.msgRefresh, "")
    }

    readonly property int msgGeometry: 4
    readonly property int msgSetTeam:  5
    readonly property int msgState:    101
    readonly property int msgHello:    1
    readonly property int msgShowGame: 2
    readonly property int msgShowTeam: 3
    readonly property int msgRefresh:  6

    // Whole application state, pushed by the backend.
    property var s: ({})

    AppLoad {
        id: appload
        applicationID: "mlb-scoreboard"
        onMessageReceived: (type, contents) => {
            if (type !== root.msgState)
                return
            try {
                root.s = JSON.parse(contents)
            } catch (e) {
                console.log("scoreboard: bad state payload:", e)
            }
        }
        Component.onCompleted: {
            appload.sendMessage(root.msgHello, "")
            root.reportGeometry()
        }
    }

    // Logos are cached on disk by the backend; load them straight off the
    // filesystem rather than pushing image data across the socket.
    function logoFor(id) {
        return (id > 0)
            ? "file:///home/root/.cache/scoreboard-logos/" + id + ".svg"
            : ""
    }

    // --- geometry ----------------------------------------------------------
    // Lay out against a fixed design canvas so every tuned size still holds,
    // then scale that canvas into the window we are given.
    //
    // xochitl does not rotate an AppLoad window, and the window stays portrait
    // however the tablet is held -- so turning the device did nothing and the
    // board stayed sideways. The backend reads the accelerometer and sends the
    // angle; we rotate the canvas by it, exactly as the fullscreen build did.
    // Orientation comes from the window, not the accelerometer. AppLoad flips
    // the window itself when the view is landscape:
    //     if (_appLoadView.width > _appLoadView.height) swap(win.width, win._height)
    // so rotating the canvas as well double-rotated it -- portrait content in a
    // landscape window, scaled right down. An earlier accelerometer version was
    // added because rotation appeared broken, but that was windowed mode, where
    // the window never flips; `disablesWindowedMode` is what actually fixed it.
    readonly property bool landscape: width > height

    readonly property real designH: 2160
    readonly property real designW:
        Math.round(designH * Math.min(width, height) / Math.max(width, height))

    readonly property real canvasW: landscape ? designH : designW
    readonly property real canvasH: landscape ? designW : designH

    // --- palette -----------------------------------------------------------
    // Gallery 3 renders colour, but muted. Keep the page white, the type black,
    // and spend colour only where it carries meaning.
    readonly property color paper:  "#FFFFFF"
    readonly property color ink:    "#000000"
    // Two greys, because e-ink needs them further apart than a screen does.
    // faint draws rules, borders and empty pips; muted is for secondary text,
    // which at #B4B4B4 was simply not visible on Gallery 3.
    // Gallery 3 renders mid-greys far lighter than a monitor does, and thin
    // strokes wash out on top of that, so secondary text sits much closer to
    // black than screen design would suggest. --muted / --faint override both.
    readonly property color faint:
        (typeof faintOverride !== "undefined" && faintOverride.length > 0)
            ? ("#" + faintOverride) : "#5A5A5A"
    readonly property color muted:
        (typeof mutedOverride !== "undefined" && mutedOverride.length > 0)
            ? ("#" + mutedOverride) : "#000000"
    readonly property color accent: "#A4123F"

    // --- geometry ----------------------------------------------------------
    // The landscape sizes below were tuned against a 1620-tall canvas (Paper
    // Pro). On the Move that canvas is only 1215 tall, and the live state --
    // score rows plus the base diamond -- then pushes the line score and footer
    // off the bottom. Shrink the vertical-heavy elements to match. Portrait has
    // the full 2160 either way, so it is left alone, and a Paper Pro lands on
    // 1.0 and renders exactly as before.
    readonly property real vScale: landscape ? Math.min(1, canvasH / 1620) : 1

    // Text and spacing shrink linearly with vScale, but the base diamond is a
    // graphic rather than type, so it can give up more than its share without
    // hurting legibility. On a short landscape canvas that extra squeeze is
    // what buys room for the footer.
    readonly property real diamondScale: vScale < 1 ? vScale * 0.82 : 1

    readonly property real pageMargin: landscape ? Math.round(64 * vScale) : 70
    readonly property real gutter: 110
    readonly property real contentW: canvasW - pageMargin * 2
    readonly property real colW: landscape ? (contentW - gutter) / 2 : contentW

    // --- type scale --------------------------------------------------------
    readonly property int runsSize:  Math.round((landscape ? 215 : 190) * vScale)
    readonly property int nameSize:  Math.round((landscape ?  90 :  78) * vScale)
    readonly property int nameSize2: Math.round((landscape ?  66 :  56) * vScale)
    readonly property int bodySize:  Math.round((landscape ?  44 :  38) * vScale)
    readonly property int gridSize:  Math.round((landscape ?  37 :  34) * vScale)

    // 0 = the Guardians board, 1 = today's full slate. Tap anywhere to swap.
    property int page: 0

    // Controls stay hidden until you ask for them: a permanent button is clutter
    // on a board you mostly just glance at. Tap anywhere to reveal, tap again or
    // wait to dismiss. The timeout costs one extra e-ink repaint, which is the
    // price of not leaving chrome on screen.
    property bool controlsShown: false
    Timer {
        id: controlsTimer
        interval: 6000
        onTriggered: root.controlsShown = false
    }
    function toggleControls() {
        controlsShown = !controlsShown
        if (controlsShown)
            controlsTimer.restart()
        else
            controlsTimer.stop()
    }

    // Where to land on the first state that arrives. Once only: after that the
    // page is whatever the reader last tapped.
    //   - nobody has picked a team yet  -> the picker, or the app just shows
    //     someone else's club with no hint that it was a default
    //   - the followed team is idle     -> the slate, rather than a board with
    //     nothing on it
    // `teamChosen` defaults to true so a frontend running against an older
    // backend, which never sends it, behaves exactly as it did before.
    property bool pageChosen: false
    onSChanged: {
        if (pageChosen || !v("loaded", false))
            return
        pageChosen = true
        if (!v("teamChosen", true))
            page = 2
        else if (!v("hasGame", false) && root.v("slate", []).length > 0)
            page = 1
    }


    function v(key, dflt) {
        return (s && s[key] !== undefined && s[key] !== null && s[key] !== "") ? s[key] : dflt
    }

    readonly property bool isLive:  v("abstractState", "") === "Live"
    readonly property bool isFinal: v("abstractState", "") === "Final"

    readonly property string halfInning: {
        if (!isLive) return v("statusText", "")
        return (v("inningState", "") + " " + v("inningOrdinal", "")).toUpperCase()
    }

    Item {
        id: stage
        anchors.fill: parent

        Item {
            id: canvas
            width: root.canvasW
            height: root.canvasH
            anchors.centerIn: parent
            transformOrigin: Item.Center
            scale: Math.min(stage.width / width, stage.height / height)

            ColumnLayout {
                visible: root.page === 0
                anchors.fill: parent
                anchors.margins: root.pageMargin
                anchors.topMargin: Math.round(56 * root.vScale)
                spacing: 0

                // ------------------------------------------------------- top
                StatusStrip {
                    id: strip
                    Layout.fillWidth: true
                    visible: !root.controlsShown
                    ink: root.ink
                    faint: root.faint
                    muted: root.muted
                    accent: root.accent
                    // With no game to follow the strip would otherwise be
                    // blank, which reads as broken. Show the date instead.
                    leftText: root.isLive ? root.halfInning
                            : !v("loaded", false) ? ""
                            : !v("hasGame", false) ? v("slateDate", "")
                            : root.isFinal ? ""
                            : v("statusText", "")
                    // Nothing on the right when there is no game: the body
                    // already says so, and the label just repeated it.
                    rightText: !v("loaded", false) ? ""
                             : root.isLive ? "LIVE"
                             : root.isFinal ? "FINAL"
                             : v("hasGame", false) ? "SCHEDULED" : ""
                    accentRight: root.isLive
                }

                Item { Layout.preferredHeight: root.landscape ? Math.round(40 * root.vScale) : 50 }

                // ------------------------------------------------------- body
                // Two columns side by side in landscape, stacked in portrait.
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.landscape ? 2 : 1
                    columnSpacing: root.gutter
                    rowSpacing: 0

                    // ========== left / upper: score, bases, count ==========
                    ColumnLayout {
                        Layout.preferredWidth: root.colW
                        Layout.maximumWidth: root.colW
                        Layout.alignment: Qt.AlignTop
                        spacing: 0

                        Repeater {
                            model: [
                                { name: v("awayName", "Away"),
                                  runs: v("awayRuns", 0),
                                  teamId: v("awayId", 0),
                                  rec: v("awayRecord", ""),
                                  place: v("awayStanding", ""),
                                  batting: root.isLive && v("isTopInning", false) },
                                { name: v("homeName", "Home"),
                                  runs: v("homeRuns", 0),
                                  teamId: v("homeId", 0),
                                  rec: v("homeRecord", ""),
                                  place: v("homeStanding", ""),
                                  batting: root.isLive && !v("isTopInning", false) }
                            ]

                            RowLayout {
                                Layout.fillWidth: true
                                visible: v("hasGame", false)
                                spacing: 22

                                // Batting marker: a slim bar, not a filled row —
                                // big dark areas are slow to repaint on e-ink.
                                Rectangle {
                                    Layout.preferredWidth: 14
                                    Layout.preferredHeight: root.runsSize * 0.62
                                    color: modelData.batting ? root.accent : "transparent"
                                }

                                // Greyscale so it sits inside the black-on-white
                                // palette; sized off the team name so it tracks
                                // the type scale.
                                Image {
                                    Layout.preferredWidth: root.nameSize * 1.15
                                    Layout.preferredHeight: root.nameSize * 1.15
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    source: root.logoFor(modelData.teamId)
                                    visible: source != ""
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: modelData.name
                                        color: root.ink
                                        font.pixelSize: root.nameSize
                                        font.weight: modelData.batting ? Font.Bold : Font.Normal
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    // Record and division place, the way a
                                    // scoreboard bug shows them.
                                    Text {
                                        text: {
                                            var bits = []
                                            if (modelData.rec) bits.push(modelData.rec)
                                            if (modelData.place) bits.push(modelData.place)
                                            return bits.join("   ")
                                        }
                                        visible: text.length > 0
                                        color: root.muted
                                        font.pixelSize: Math.round(root.nameSize * 0.42)
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 1
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    text: modelData.runs
                                    color: root.ink
                                    font.pixelSize: root.runsSize
                                    font.weight: Font.Bold
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: root.runsSize * 1.30
                                }
                            }
                        }

                        Item { Layout.preferredHeight: Math.round(26 * root.vScale); visible: v("hasGame", false) }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 6
                            color: root.ink
                            visible: v("hasGame", false)
                        }

                        Item { Layout.preferredHeight: root.landscape ? Math.round(80 * root.vScale) : 56 }

                        // Bases hard left, count out to the right, both centred
                        // against each other. Sitting side by side on the left
                        // they read as one clump under the score.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            visible: root.isLive

                            Item {
                                Layout.fillWidth: true
                                Layout.maximumWidth: Math.round(90 * root.vScale)
                            }

                            BaseDiamond {
                                Layout.fillWidth: false
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: root.landscape ? Math.round(470 * root.diamondScale) : 560
                                Layout.preferredHeight: root.landscape ? Math.round(470 * root.diamondScale) : 470
                                ink: root.ink
                                faint: root.faint
                                first: v("onFirst", false)
                                second: v("onSecond", false)
                                third: v("onThird", false)
                            }

                            // Slack on both sides with a capped gap between:
                            // the pair sits together near the middle rather
                            // than pinned to opposite edges.
                            Item {
                                Layout.fillWidth: true
                                Layout.maximumWidth: Math.round(150 * root.vScale)
                            }

                            ColumnLayout {
                                Layout.fillWidth: false
                                Layout.alignment: Qt.AlignVCenter
                                spacing: root.landscape ? Math.round(30 * root.vScale) : 26

                                Repeater {
                                    model: [
                                        { l: "BALLS",   n: v("balls", 0),   t: 4 },
                                        { l: "STRIKES", n: v("strikes", 0), t: 3 },
                                        { l: "OUTS",    n: v("outs", 0),    t: 3 }
                                    ]
                                    CountRow {
                                        label: modelData.l
                                        value: modelData.n
                                        total: modelData.t
                                        ink: root.ink
                                        faint: root.faint
                                        pip: root.landscape ? Math.round(40 * root.vScale) : 42
                                        labelWidth: root.landscape ? Math.round(190 * root.vScale) : 215
                                        gap: root.landscape ? Math.round(18 * root.vScale) : 22
                                    }
                                }
                            }
                        }
                    }

                    // ========== right / lower: matchup and last play ========
                    ColumnLayout {
                        Layout.preferredWidth: root.colW
                        Layout.maximumWidth: root.colW
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: root.landscape ? 0 : 60
                        spacing: 0

                        GridLayout {
                            Layout.fillWidth: true
                            visible: root.isLive
                            columns: 2
                            columnSpacing: 32
                            rowSpacing: 10

                            Text {
                                text: "AT BAT"
                                // faint is for rules; type needs muted or it
                                // disappears on Gallery 3.
                                color: root.muted
                                font.pixelSize: Math.round(30 * root.vScale)
                                font.letterSpacing: 4
                                Layout.preferredWidth: Math.round(210 * root.vScale)
                            }
                            Text {
                                text: v("batter", "—")
                                color: root.ink
                                font.pixelSize: root.nameSize2
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "PITCHING"
                                // faint is for rules; type needs muted or it
                                // disappears on Gallery 3.
                                color: root.muted
                                font.pixelSize: Math.round(30 * root.vScale)
                                font.letterSpacing: 4
                                Layout.preferredWidth: Math.round(210 * root.vScale)
                            }
                            Text {
                                text: v("pitcher", "—")
                                color: root.ink
                                font.pixelSize: root.nameSize2
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }

                        Item { Layout.preferredHeight: Math.round(22 * root.vScale); visible: root.isLive }

                        // The pitch that was just thrown.
                        Text {
                            Layout.fillWidth: true
                            visible: root.isLive && text.length > 0
                            text: root.v("lastPitch", "")
                            color: root.ink
                            font.pixelSize: Math.round(root.bodySize * 0.82)
                            font.bold: true
                            font.letterSpacing: 2
                            elide: Text.ElideRight
                        }

                        Item { Layout.preferredHeight: Math.round(22 * root.vScale); visible: root.isLive }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3
                            color: root.faint
                            visible: root.isLive
                        }

                        Item { Layout.preferredHeight: Math.round(32 * root.vScale); visible: root.isLive }

                        Text {
                            Layout.fillWidth: true
                            visible: (root.isLive || root.isFinal) && text.length > 0
                            text: v("lastPlay", "")
                            color: root.ink
                            font.pixelSize: root.bodySize
                            font.italic: true
                            lineHeight: 1.3
                            wrapMode: Text.WordWrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                        }

                        // ---- pitchers of record, once the game is over ----
                        Item {
                            Layout.preferredHeight: Math.round(34 * root.vScale)
                            visible: root.isFinal && root.v("pitchers", []).length > 0
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: root.isFinal && root.v("pitchers", []).length > 0
                            spacing: Math.round(10 * root.vScale)

                            Repeater {
                                model: root.v("pitchers", [])

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Math.round(14 * root.vScale)

                                    Text {
                                        text: modelData.label
                                        color: root.muted
                                        font.pixelSize: root.bodySize
                                        font.bold: true
                                        Layout.preferredWidth: Math.round(44 * root.vScale)
                                    }
                                    Text {
                                        text: modelData.name
                                        color: root.ink
                                        font.pixelSize: root.bodySize
                                        font.bold: true
                                        Layout.preferredWidth: Math.round(300 * root.vScale)
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: modelData.line
                                        color: root.muted
                                        font.pixelSize: root.bodySize
                                        horizontalAlignment: Text.AlignRight
                                        Layout.preferredWidth: Math.round(130 * root.vScale)
                                    }
                                    Text {
                                        text: modelData.era
                                        color: root.muted
                                        font.pixelSize: root.bodySize
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: !root.isLive && !root.isFinal
                            // Said together with the league count, "No games
                            // scheduled today" read as a contradiction. The
                            // followed team being idle is the only fact here.
                            text: {
                                if (!v("loaded", false))
                                    return "Loading…"
                                if (v("hasGame", false))
                                    return v("venue", "")
                                var n = root.v("slate", []).length
                                return n > 0
                                    ? "Guardians are off today — " + n
                                      + " games around the league"
                                    : "No games scheduled today"
                            }
                            color: root.muted
                            font.pixelSize: root.landscape ? 42 : 48
                            lineHeight: 1.3
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Whitespace lives here, between the body and the line score,
                // so neither can collide with the other.
                Item { Layout.fillHeight: true; Layout.minimumHeight: Math.round(50 * root.vScale) }

                // ------------------------------------------- line score (full width)
                LineScore {
                    Layout.fillWidth: true
                    muted: root.muted
                    visible: v("hasGame", false) && (root.isLive || root.isFinal)
                    ink: root.ink
                    faint: root.faint
                    fontSize: root.gridSize
                    teamColWidth: 160
                    innings: v("innings", [])
                    awayAbbr: v("awayAbbr", "")
                    homeAbbr: v("homeAbbr", "")
                    awayR: v("awayRuns", 0);   homeR: v("homeRuns", 0)
                    awayH: v("awayHits", 0);   homeH: v("homeHits", 0)
                    awayE: v("awayErrors", 0); homeE: v("homeErrors", 0)
                }

                Item { Layout.preferredHeight: Math.round(44 * root.vScale) }

                // ---------------------------------------------------- footer
                StatusStrip {
                    Layout.fillWidth: true
                    ink: root.muted
                    faint: root.faint
                    muted: root.muted
                    leftText: v("venue", "")
                    rightText: v("error", "") !== ""
                               ? "OFFLINE"
                               : ("UPDATED " + v("updatedAt", "—"))
                }
            }

            // The only tap target on either page: the strip across the top.
            // It lives on the canvas rather than inside the board layout,
            // otherwise the game list -- which replaces that layout -- has
            // nothing to tap.
            Item {
                id: topBar
                z: 30
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.pageMargin
                anchors.rightMargin: root.pageMargin
                anchors.topMargin: Math.round(40 * root.vScale)
                height: Math.round(86 * root.vScale)

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.toggleControls()
                }

                // Only BACK lives here. Closing is AppLoad's job: swiping down
                // from the top reveals its own bar with minimize/maximize/close,
                // which is hardcoded for any maximized app (window.qml,
                // swipeFromTheTop) and cannot be turned off from a manifest.
                // Offering our own close as well was just two ways to do one
                // thing; going back to the game list is the part AppLoad has no
                // answer for.
                //
                // Opaque, so whatever header sits beneath is hidden while the
                // menu is up -- on the board that is the inning/LIVE line, on
                // the list it is the date.
                Rectangle {
                    anchors.fill: parent
                    color: root.paper
                    visible: root.controlsShown
                }

                Text {
                    visible: root.controlsShown && root.page !== 1
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "BACK"
                    color: root.muted
                    font.pixelSize: Math.round(30 * root.vScale)
                    font.bold: true
                    font.letterSpacing: 3

                    MouseArea {
                        anchors.centerIn: parent
                        width: parent.width + Math.round(90 * root.vScale)
                        height: Math.round(86 * root.vScale)
                        onClicked: {
                            root.page = 1
                            root.controlsShown = false
                            controlsTimer.stop()
                        }
                    }
                }

                Text {
                    visible: root.controlsShown && root.page !== 2
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "TEAM"
                    color: root.muted
                    font.pixelSize: Math.round(30 * root.vScale)
                    font.bold: true
                    font.letterSpacing: 3

                    MouseArea {
                        anchors.centerIn: parent
                        width: parent.width + Math.round(90 * root.vScale)
                        height: Math.round(86 * root.vScale)
                        onClicked: {
                            root.page = 2
                            root.controlsShown = false
                            controlsTimer.stop()
                        }
                    }
                }
            }

            // ------------------------------------------------- page 2
            TeamPicker {
                visible: root.page === 2
                anchors.fill: parent
                anchors.margins: root.pageMargin
                anchors.topMargin: Math.round(150 * root.vScale)
                ink: root.ink
                faint: root.faint
                muted: root.muted
                accent: root.accent
                u: root.vScale
                teams: root.v("teams", [])
                currentId: root.v("teamId", 0)
                logoSource: root.logoFor
                onTeamChosen: function (id) {
                    appload.sendMessage(root.msgSetTeam, String(id))
                    root.page = 0
                }
            }

            // ------------------------------------------------- page 1
            Slate {
                visible: root.page === 1
                anchors.fill: parent
                anchors.margins: root.pageMargin
                anchors.topMargin: Math.round(56 * root.vScale)
                ink: root.ink
                faint: root.faint
                muted: root.muted
                accent: root.accent
                games: root.v("slate", [])
                dateLabel: root.v("slateDate", "")
                headerVisible: !root.controlsShown
                loaded: root.v("loaded", false)
                onGamePicked: function (row) {
                    appload.sendMessage(root.msgShowGame, String(row.gamePk))
                    root.page = 0
                }
                u: root.vScale
                logoSource: root.logoFor
            }
        }

    }
}
