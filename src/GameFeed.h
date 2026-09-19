#pragma once

#include <QObject>
#include <QVariantMap>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QHash>
#include <QJsonObject>
#include <functional>

/*
 * Pulls live game state from MLB's public Stats API (statsapi.mlb.com).
 * No key, no auth. Two endpoints:
 *
 *   /api/v1/schedule?sportId=1&teamId=..&date=..&hydrate=team
 *       -> today's gamePk, team names, venue, game status
 *   /api/v1/game/{gamePk}/linescore
 *       -> score, inning, count, outs, runners, batter, pitcher  (a few KB)
 *   /api/v1/game/{gamePk}/playByPlay?fields=...
 *       -> description of the play that just happened
 *   /api/v1/schedule?sportId=1&date=..&hydrate=team,linescore
 *       -> every game today: score, state, inning and both teams' records,
 *          which is the whole slate page in one request
 *
 * Everything lands in a single `state` QVariantMap that QML reads directly,
 * so there is no model plumbing to maintain.
 */
class GameFeed : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

public:
    // A non-empty demoState ("live", "pregame", "final", "none") fills in a
    // canned game and makes no network calls, so the layout can be worked on
    // when nothing is being played.
    explicit GameFeed(int teamId, const QString &demoState = QString(),
                      QObject *parent = nullptr);

    QVariantMap state() const { return m_state; }

public slots:
    void refresh();

    // Show any game from the slate, not just the followed team's. Pins the
    // feed to that gamePk until showTeamGame() releases it, so the schedule
    // poll cannot quietly drag the board back to the team's own game.
    Q_INVOKABLE void showGame(double gamePk, const QVariantMap &info);
    Q_INVOKABLE void showTeamGame();

    // Follow a different team. Clears any pinned game and refetches.
    void setTeam(int teamId);
    int team() const { return m_teamId; }

    // The 30 clubs, for the team picker. Fetched once.
    void requestTeamList();

signals:
    void stateChanged();

private:
    void get(const QString &url, std::function<void(const QJsonObject &)> cb);
    void requestSchedule();
    void requestLinescore();
    void requestLastPlay();
    void requestSlate();
    void requestStandings();
    void requestDecisions();
    void applyStandings();
    void publish();
    void loadDemoGame(const QString &demoState);

    int m_teamId;
    qint64 m_gamePk = 0;
    bool m_pinned = false;
    QNetworkAccessManager m_net;
    QTimer m_pollTimer;
    QTimer m_scheduleTimer;
    QTimer m_standingsTimer;
    // teamId -> "1st AL Central". Standings move slowly; fetched twice an hour.
    QHash<int, QString> m_standing;
    QVariantMap m_state;
};
