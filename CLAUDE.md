# Context for Claude Code

An MLB scoreboard for reMarkable tablets, installed as a native AppLoad app.
It follows one team, shows live score, count, runners, matchup, last pitch and
line score, and lists the day's whole slate.

**Read [docs/PLATFORM.md](docs/PLATFORM.md) first.** Everything about the
device, the SDK, AppLoad packaging, the socket protocol, TLS, rotation and
e-ink design lives there. This file is only what is specific to baseball.

That doc is kept **byte-identical** in the `football-scoreboard` repo, which
builds the NFL and college football apps from the same platform knowledge. It
has already drifted once by being edited in one place only — if you change it,
change it in both.

## Layout

```
src/backend_main.cpp    message loop, settings, launcher icon
src/AppLoadLink.{h,cpp} the AppLoad socket protocol
src/GameFeed.{h,cpp}    MLB Stats API; flattens everything into one QVariantMap
src/LogoStore.{h,cpp}   team SVGs, fetched once and converted to greyscale
appload-native/ui/      the QML frontend
appload-native/manifest.json, icon.png
build.sh                cross-compile the backend in the SDK container
package.sh              build + qmllint + rcc -> dist/mlb-scoreboard.zip
install.sh              push a built zip to a tablet over SSH
```

QML reads `feed.state.<key>`. Everything flows through that one map — there is
no model plumbing, so adding a field means one line in `GameFeed.cpp` and one
binding in QML.

Message ids across the socket: `Hello=1`, `ShowGame=2`, `ShowTeam=3`,
`Geometry=4`, `SetTeam=5` to the backend; `State=101` back.

## Commands

```bash
./build.sh ~/Downloads/remarkable-...-chiappa-...-toolchain.sh   # first run ~20 min
./package.sh                                                      # -> dist/mlb-scoreboard.zip
RM_HOST=10.11.99.1 ./install.sh
```

Frontend changes need `systemctl restart xochitl` on the device to take effect;
backend changes do not. See PLATFORM.md.

## Pages

0. **Board** — score, count, bases, matchup, last pitch, line score.
1. **Slate** — the day's games as a grid of `GameCard.qml`, three columns in
   landscape and two in portrait. Cards carry status, both teams with
   record and score, and for a live game the bases and outs — all from one
   `schedule?hydrate=team,linescore` call, which already includes `balls`,
   `strikes`, `outs` and `offense`. Losers of finished games drop to grey.
   **Tapping a card opens that game on the board**, so any game is reachable,
   not just the followed team's.
2. **TeamPicker** — all 30 clubs. The pick persists to
   `/home/root/.config/scoreboard/settings.json` and rewrites the launcher icon,
   which only appears after a restart; the screen says so up front.

Interaction: the top strip is the only tap target, and tapping it reveals BACK
and TEAM. No long-press — a long press used to quit, and it reads as a crash.
Card widths are proportional (`width * 0.115` etc.), not fixed, or the base
diamond clips off the card in portrait.

## MLB Stats API

`statsapi.mlb.com`, keyless, and it honours `fields=` — use it, the unfiltered
payloads are large. TLS needs the workaround in PLATFORM.md.

- **Dates are US Eastern**, and the device clock is UTC. `GameFeed.cpp` computes
  the schedule date in `America/New_York` and rolls back before 6am, or it asks
  for tomorrow's schedule all evening.
- **Last pitch** ("SPLITTER · 82.8 · SWINGING STRIKE") comes from `playByPlay`
  filtered to `currentPlay,result,description,playEvents,isPitch,details,type,
  pitchData,startSpeed` — about 60 bytes. Walk `playEvents` backwards for the
  last entry with `isPitch`. Between batters `currentPlay` carries no events at
  all, so hold the previous line rather than blanking it every half inning.
- **Division place** comes from `standings?leagueId=103,104&hydrate=division`.
  The hydrate is what turns a division id into "AL Central". Refreshed every 30
  minutes into a teamId -> "1st AL Central" map.
- **Pitchers of record** once a game is final: `decisions` hydrates onto
  `schedule`, but stats do **not** hydrate underneath it —
  `decisions(person(stats(...)))` silently returns no stats. Fetch the ids, then
  batch all three through
  `people?personIds=a,b,c&hydrate=stats(group=pitching,type=season)`.
- Poll intervals: 15s live game, 10min schedule, 30min standings.

Selecting a game sets `m_pinned`, and `requestSchedule()` returns early while
pinned — otherwise the ten-minute poll drags the board back to the followed
team's game under the reader. `showTeamGame()` releases it. Slate cards carry
`gamePk`, both team names, venue and status so a tapped card paints the board
immediately, before its linescore request returns.

An explicit team pick must always act, even when the id is unchanged. Guarding
on `id != feed.team()` made picking the already-default team a silent no-op.

## Logos

`LogoStore.cpp` fetches `mlbstatic.com/team-logos/<id>.svg` once per team and
caches a greyscale copy under `~/.cache/scoreboard-logos`. They never cross the
socket; QML loads `file:///home/root/.cache/scoreboard-logos/<id>.svg`.

- Colours appear both as `fill="#abc"` attributes and, on one team of thirty
  (Pittsburgh), as CSS inside a `<style>` block. Handle both or that logo stays
  in full colour.
- In the hex regex the **6-digit branch must come first**. With `{3}` leading,
  `#ffce34` matches as `ffc` wherever no delimiter forces backtracking, which
  turned the Pirates' gold into white.
- Fills map to Rec.601 luma. Near-white (>=245) stays white because it is
  knock-out space — the Cubs' inner disc — and everything else is capped at 150
  so it stays readable on a white page.

The shipped `icon.png` is neutral. A club logo is only written to it after the
user picks that club, so no trademarked mark is redistributed.

## Layout constants that have already bitten

- `BaseDiamond.qml` pins `cell` at 0.26 of the box. Rotated squares above ~0.28
  overlap each other.
- `LineScore.qml` always lays out **at least nine innings**, blank until played.
  Sizing columns off however many innings exist so far made the grid reflow
  every time one finished — wide cells in the 1st, cramped by the 9th.
- The line score is a full-width row *below* the two landscape columns, not
  inside the right one. It collided with the footer when it lived in the column.
- Only the top strip is a tap target on the board. An earlier full-page
  `TapHandler` fired together with the card handlers, so one tap on the slate
  bounced straight back.
- The app lands on the slate once, at startup, when the followed team has no
  game — a board with nothing on it is a bad first screen. After that the page
  is whatever the reader last tapped (`pageChosen` in `Main.qml`).

## Conventions

- Colour is black on white with one accent (`#A4123F`), used only where it
  carries meaning — currently the batting-team marker and the LIVE label.
- **Two greys, and they are not interchangeable.** `faint` (`#5A5A5A`) draws
  rules, borders and empty pips; `muted` (`#000000`) is for secondary text —
  records, division place, inning numbers, W/L/S labels, the footer. Picking by
  role is the rule: stroke takes `faint`, type takes `muted`. `faint` has been
  used for text three times by mistake and was unreadable on the panel each
  time. `GameCard` keeps its own `dimmed` (`#4A4A4A`) for the loser of a
  finished game; that is a separate idea, and folding it into `muted` flattened
  the distinction when `muted` went black. It was `#8A8A8A` until the football
  app established on the panel that that grey is not readable.
- Two names to avoid: `final` is reserved in QML, `slots` is a Qt macro that
  expands to nothing in C++. Both fail with parse errors naming the wrong thing.
- The owner does not want Claude named in commit messages, branch names, or code
  comments, and does not want co-author or "generated with" trailers.
