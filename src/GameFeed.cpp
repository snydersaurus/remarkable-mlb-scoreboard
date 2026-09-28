#include "GameFeed.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include <QLocale>
#include <algorithm>
#include <QSslSocket>
#include <QUrl>

namespace {
const char *kApi = "https://statsapi.mlb.com/api/v1";

// Live games refresh fast; the schedule barely changes.
constexpr int kPollMs     = 15 * 1000;
constexpr int kScheduleMs = 10 * 60 * 1000;

// A request that never answers has to fail eventually, or the poll behind it
// has nothing to retry. Qt sets no transfer timeout by default.
constexpr int kTimeoutMs  = 20 * 1000;

// More wall-clock than this between two polls means the tablet was asleep,
// not that a request was slow.
constexpr int kSleptSecs  = 90;

// MLB keys its schedule to US Eastern dates, and the tablet's clock is UTC
// (/etc/localtime -> Universal), so the device's own date rolls over at 8pm
// Eastern -- in the middle of a night game. Ask in the zone the schedule is
// actually keyed to.
//
// Before 6am Eastern we still want last night's game: a west-coast road game
// starting 10pm ET is dated the previous day by MLB and is often still being
// played after midnight. Nothing is ever scheduled before 6am, so rolling back
// costs nothing and keeps a finished game on screen instead of "no game today".
QDateTime easternNow()
{
    const QTimeZone eastern("America/New_York");
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    return eastern.isValid() ? utc.toTimeZone(eastern)
                             : utc.addSecs(-5 * 60 * 60);
}

QString scheduleDate()
{
    const QDateTime et = easternNow();
    QDate d = et.date();
    if (et.time().hour() < 6)
        d = d.addDays(-1);
    return d.toString(QStringLiteral("yyyy-MM-dd"));
}

QVariant inningCell(const QJsonObject &side)
{
    if (!side.contains("runs"))
        return QVariant(QStringLiteral("-"));
    return QVariant(side.value("runs").toInt());
}
} // namespace

GameFeed::GameFeed(int teamId, const QString &demoState, QObject *parent)
    : QObject(parent), m_teamId(teamId)
{
    m_state[QStringLiteral("hasGame")]    = false;
    m_state[QStringLiteral("statusText")] = QStringLiteral("Loading");
    m_state[QStringLiteral("error")]      = QString();
    // Until the first reply lands we know nothing. Without this the UI cannot
    // tell "still asking" from "asked, and there is genuinely no game", and it
    // spends the first seconds after launch claiming there is no game.
    m_state[QStringLiteral("loaded")]     = false;

    if (!demoState.isEmpty()) {
        loadDemoGame(demoState);
        return;
    }

    // Measured on the device with the football app, which shares this design:
    // after a fifteen minute suspend a reused keep-alive socket sat
    // ESTABLISHED with 1761 bytes stuck in its send queue, retransmitting into
    // a connection whose far end was long gone. Without a timeout that request
    // hung forever and the board stopped updating.
    m_net.setTransferTimeout(kTimeoutMs);

    qInfo("tls: supportsSsl=%d build=%s runtime=%s",
          QSslSocket::supportsSsl(),
          qPrintable(QSslSocket::sslLibraryBuildVersionString()),
          qPrintable(QSslSocket::sslLibraryVersionString()));

    connect(&m_pollTimer, &WakeTimer::timeout, this, &GameFeed::refresh);
    m_pollTimer.start(kPollMs);

    connect(&m_scheduleTimer, &WakeTimer::timeout, this, &GameFeed::requestSchedule);
    m_scheduleTimer.start(kScheduleMs);

    connect(&m_standingsTimer, &WakeTimer::timeout, this, &GameFeed::requestStandings);
    m_standingsTimer.start(30 * 60 * 1000);

    requestSchedule();
    requestSlate();
    requestStandings();
}

void GameFeed::get(const QString &url, std::function<void(const QJsonObject &)> cb)
{
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "rmpp-scoreboard/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("request failed: %s -> %s",
                     qPrintable(url), qPrintable(reply->errorString()));
            m_state[QStringLiteral("error")] = reply->errorString();
            publish();
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject())
            return;
        m_state[QStringLiteral("error")] = QString();
        cb(doc.object());
    });
}

void GameFeed::requestSchedule()
{
    const QString date = scheduleDate();
    const QString url = QStringLiteral("%1/schedule?sportId=1&teamId=%2&date=%3&hydrate=team")
                            .arg(QString::fromLatin1(kApi))
                            .arg(m_teamId)
                            .arg(date);

    get(url, [this](const QJsonObject &o) {
        // A game picked from the slate outranks the followed team's game.
        if (m_pinned)
            return;

        const QJsonArray dates = o.value(QStringLiteral("dates")).toArray();
        const QJsonArray games = dates.isEmpty()
            ? QJsonArray()
            : dates.first().toObject().value(QStringLiteral("games")).toArray();

        m_state[QStringLiteral("loaded")] = true;
        m_state[QStringLiteral("teamId")] = m_teamId;

        if (games.isEmpty()) {
            m_gamePk = 0;
            m_state[QStringLiteral("hasGame")]    = false;
            m_state[QStringLiteral("statusText")] = QStringLiteral("No game today");
            publish();
            return;
        }

        const QJsonObject g = games.first().toObject();
        m_gamePk = static_cast<qint64>(g.value(QStringLiteral("gamePk")).toDouble());

        const QJsonObject teams  = g.value(QStringLiteral("teams")).toObject();
        const QJsonObject away   = teams.value(QStringLiteral("away")).toObject()
                                        .value(QStringLiteral("team")).toObject();
        const QJsonObject home   = teams.value(QStringLiteral("home")).toObject()
                                        .value(QStringLiteral("team")).toObject();
        const QJsonObject status = g.value(QStringLiteral("status")).toObject();

        m_state[QStringLiteral("awayName")] = away.value(QStringLiteral("teamName")).toString();
        m_state[QStringLiteral("homeName")] = home.value(QStringLiteral("teamName")).toString();
        auto rec = [](const QJsonObject &sideObj) {
            const QJsonObject r = sideObj.value(QStringLiteral("leagueRecord")).toObject();
            return QStringLiteral("%1-%2").arg(r.value(QStringLiteral("wins")).toInt())
                                          .arg(r.value(QStringLiteral("losses")).toInt());
        };
        m_state[QStringLiteral("awayRecord")] = rec(teams.value(QStringLiteral("away")).toObject());
        m_state[QStringLiteral("homeRecord")] = rec(teams.value(QStringLiteral("home")).toObject());
        m_state[QStringLiteral("awayId")]   = away.value(QStringLiteral("id")).toInt();
        m_state[QStringLiteral("homeId")]   = home.value(QStringLiteral("id")).toInt();
        m_state[QStringLiteral("awayAbbr")] = away.value(QStringLiteral("abbreviation")).toString();
        m_state[QStringLiteral("homeAbbr")] = home.value(QStringLiteral("abbreviation")).toString();
        m_state[QStringLiteral("venue")]    = g.value(QStringLiteral("venue")).toObject()
                                               .value(QStringLiteral("name")).toString();
        m_state[QStringLiteral("abstractState")] =
            status.value(QStringLiteral("abstractGameState")).toString();
        m_state[QStringLiteral("statusText")] =
            status.value(QStringLiteral("detailedState")).toString();
        m_state[QStringLiteral("hasGame")] = true;
        m_state[QStringLiteral("pitchers")] = QVariantList();
        if (status.value(QStringLiteral("abstractGameState")).toString() != QStringLiteral("Live"))
            m_state[QStringLiteral("lastPitch")] = QString();
        applyStandings();

        publish();
        refresh();
    });
}

void GameFeed::refresh()
{
    // TCP connections do not survive the tablet sleeping, but Qt does not know
    // that and will reuse one from its keep-alive pool. If more time has
    // passed than a poll interval can explain, assume we were asleep and throw
    // the pool away rather than write into a dead socket.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_lastRefresh > 0 && (now - m_lastRefresh) > kSleptSecs) {
        qInfo("woke after %lld s -- dropping stale connections",
              static_cast<long long>(now - m_lastRefresh));
        m_net.clearConnectionCache();
    }
    m_lastRefresh = now;


    requestSlate();
    if (m_gamePk == 0) {
        requestSchedule();
        return;
    }
    requestLinescore();
    requestLastPlay();
}

void GameFeed::requestLinescore()
{
    const QString url = QStringLiteral("%1/game/%2/linescore")
                            .arg(QString::fromLatin1(kApi)).arg(m_gamePk);

    get(url, [this](const QJsonObject &o) {
        const QJsonObject teams   = o.value(QStringLiteral("teams")).toObject();
        const QJsonObject away    = teams.value(QStringLiteral("away")).toObject();
        const QJsonObject home    = teams.value(QStringLiteral("home")).toObject();
        const QJsonObject offense = o.value(QStringLiteral("offense")).toObject();
        const QJsonObject defense = o.value(QStringLiteral("defense")).toObject();

        m_state[QStringLiteral("awayRuns")]   = away.value(QStringLiteral("runs")).toInt();
        m_state[QStringLiteral("homeRuns")]   = home.value(QStringLiteral("runs")).toInt();
        m_state[QStringLiteral("awayHits")]   = away.value(QStringLiteral("hits")).toInt();
        m_state[QStringLiteral("homeHits")]   = home.value(QStringLiteral("hits")).toInt();
        m_state[QStringLiteral("awayErrors")] = away.value(QStringLiteral("errors")).toInt();
        m_state[QStringLiteral("homeErrors")] = home.value(QStringLiteral("errors")).toInt();

        m_state[QStringLiteral("inningOrdinal")] =
            o.value(QStringLiteral("currentInningOrdinal")).toString();
        m_state[QStringLiteral("inningState")] =
            o.value(QStringLiteral("inningState")).toString();
        m_state[QStringLiteral("isTopInning")] =
            o.value(QStringLiteral("isTopInning")).toBool();

        m_state[QStringLiteral("balls")]   = o.value(QStringLiteral("balls")).toInt();
        m_state[QStringLiteral("strikes")] = o.value(QStringLiteral("strikes")).toInt();
        m_state[QStringLiteral("outs")]    = o.value(QStringLiteral("outs")).toInt();

        // A base key exists ONLY when that base is occupied. That is the whole trick.
        m_state[QStringLiteral("onFirst")]  = offense.contains(QStringLiteral("first"));
        m_state[QStringLiteral("onSecond")] = offense.contains(QStringLiteral("second"));
        m_state[QStringLiteral("onThird")]  = offense.contains(QStringLiteral("third"));

        m_state[QStringLiteral("batter")] = offense.value(QStringLiteral("batter"))
                                                   .toObject()
                                                   .value(QStringLiteral("fullName")).toString();
        m_state[QStringLiteral("pitcher")] = defense.value(QStringLiteral("pitcher"))
                                                    .toObject()
                                                    .value(QStringLiteral("fullName")).toString();

        QVariantList innings;
        // Pitchers of record only exist once the game is over.
        if (m_state.value(QStringLiteral("abstractState")).toString() == QStringLiteral("Final")
            && m_state.value(QStringLiteral("pitchers")).toList().isEmpty())
            requestDecisions();

        const QJsonArray arr = o.value(QStringLiteral("innings")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject i = v.toObject();
            QVariantMap row;
            row[QStringLiteral("num")]  = i.value(QStringLiteral("num")).toInt();
            row[QStringLiteral("away")] = inningCell(i.value(QStringLiteral("away")).toObject());
            row[QStringLiteral("home")] = inningCell(i.value(QStringLiteral("home")).toObject());
            innings.append(row);
        }
        m_state[QStringLiteral("innings")] = innings;

        publish();
    });
}

void GameFeed::showGame(double gamePk, const QVariantMap &info)
{
    const qint64 pk = qint64(gamePk);
    if (pk <= 0)
        return;

    m_pinned = true;
    m_gamePk = pk;

    // Paint what the slate already knows straight away, so the board is never
    // blank while the linescore request is in flight.
    auto carry = [&](const char *from, const char *to) {
        if (info.contains(QLatin1String(from)))
            m_state[QString::fromLatin1(to)] = info.value(QLatin1String(from));
    };
    carry("awayName", "awayName");
    carry("homeName", "homeName");
    carry("awayAbbr", "awayAbbr");
    carry("homeAbbr", "homeAbbr");
    carry("awayId",   "awayId");
    carry("homeId",   "homeId");
    carry("venue",    "venue");
    carry("statusText", "statusText");
    carry("abstract", "abstractState");
    carry("awayRuns", "awayRuns");
    carry("homeRuns", "homeRuns");
    carry("awayRecord", "awayRecord");
    carry("homeRecord", "homeRecord");
    m_state[QStringLiteral("pitchers")] = QVariantList();
    m_state[QStringLiteral("lastPitch")] = QString();
    applyStandings();

    m_state[QStringLiteral("hasGame")] = true;
    m_state[QStringLiteral("innings")] = QVariantList();
    m_state[QStringLiteral("lastPlay")] = QString();
    publish();

    requestLinescore();
    requestLastPlay();
}

void GameFeed::showTeamGame()
{
    if (!m_pinned)
        return;
    m_pinned = false;
    m_gamePk = 0;
    requestSchedule();
}

void GameFeed::applyStandings()
{
    auto place = [this](const char *idKey, const char *outKey) {
        const int id = m_state.value(QString::fromLatin1(idKey)).toInt();
        m_state[QString::fromLatin1(outKey)] = m_standing.value(id);
    };
    place("awayId", "awayStanding");
    place("homeId", "homeStanding");
}

void GameFeed::setTeam(int teamId)
{
    if (teamId <= 0 || teamId == m_teamId)
        return;

    m_teamId = teamId;
    m_pinned = false;
    m_gamePk = 0;

    // Nothing from the old team should survive the switch.
    m_state[QStringLiteral("hasGame")]   = false;
    m_state[QStringLiteral("pitchers")]  = QVariantList();
    m_state[QStringLiteral("innings")]   = QVariantList();
    m_state[QStringLiteral("lastPlay")]  = QString();
    m_state[QStringLiteral("lastPitch")] = QString();
    m_state[QStringLiteral("loaded")]    = false;

    publish();
    requestSchedule();
}

void GameFeed::requestTeamList()
{
    const QString url = QStringLiteral("%1/teams?sportId=1&activeStatus=Y")
                            .arg(QString::fromLatin1(kApi));

    get(url, [this](const QJsonObject &o) {
        QVariantList teams;
        for (const QJsonValue &v : o.value(QStringLiteral("teams")).toArray()) {
            const QJsonObject t = v.toObject();
            const int id = t.value(QStringLiteral("id")).toInt();
            if (id <= 0)
                continue;
            QVariantMap m;
            m[QStringLiteral("id")]   = id;
            m[QStringLiteral("name")] = t.value(QStringLiteral("name")).toString();
            m[QStringLiteral("abbr")] = t.value(QStringLiteral("abbreviation")).toString();
            teams.append(m);
        }

        std::sort(teams.begin(), teams.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("name")).toString()
                 < b.toMap().value(QStringLiteral("name")).toString();
        });

        m_state[QStringLiteral("teams")] = teams;
        publish();
    });
}

void GameFeed::requestStandings()
{
    // hydrate=division turns the division id into "AL Central".
    const QString url = QStringLiteral(
        "%1/standings?leagueId=103,104&season=%2&standingsTypes=regularSeason&hydrate=division")
        .arg(QString::fromLatin1(kApi))
        .arg(easternNow().date().year());

    get(url, [this](const QJsonObject &o) {
        auto ordinal = [](int n) {
            if (n <= 0) return QString();
            const int mod100 = n % 100;
            if (mod100 >= 11 && mod100 <= 13) return QStringLiteral("%1th").arg(n);
            switch (n % 10) {
            case 1:  return QStringLiteral("%1st").arg(n);
            case 2:  return QStringLiteral("%1nd").arg(n);
            case 3:  return QStringLiteral("%1rd").arg(n);
            default: return QStringLiteral("%1th").arg(n);
            }
        };

        const QJsonArray records = o.value(QStringLiteral("records")).toArray();
        for (const QJsonValue &rv : records) {
            const QJsonObject r = rv.toObject();
            const QJsonObject div = r.value(QStringLiteral("division")).toObject();
            QString divName = div.value(QStringLiteral("nameShort")).toString();
            if (divName.isEmpty())
                divName = div.value(QStringLiteral("name")).toString();

            for (const QJsonValue &tv : r.value(QStringLiteral("teamRecords")).toArray()) {
                const QJsonObject t = tv.toObject();
                const int id = t.value(QStringLiteral("team")).toObject()
                                .value(QStringLiteral("id")).toInt();
                const int rank = t.value(QStringLiteral("divisionRank")).toString().toInt();
                if (id > 0 && rank > 0)
                    m_standing.insert(id, QStringLiteral("%1 %2").arg(ordinal(rank), divName));
            }
        }
        applyStandings();
        publish();
    });
}

void GameFeed::requestDecisions()
{
    if (m_gamePk <= 0)
        return;

    const QString url = QStringLiteral("%1/schedule?sportId=1&gamePk=%2&hydrate=decisions")
                            .arg(QString::fromLatin1(kApi)).arg(m_gamePk);

    get(url, [this](const QJsonObject &o) {
        const QJsonArray dates = o.value(QStringLiteral("dates")).toArray();
        if (dates.isEmpty())
            return;
        const QJsonArray games = dates.first().toObject().value(QStringLiteral("games")).toArray();
        if (games.isEmpty())
            return;
        const QJsonObject d = games.first().toObject().value(QStringLiteral("decisions")).toObject();
        if (d.isEmpty())
            return;

        // One people call covers winner, loser and save together.
        // Not named "slots": Qt defines that as a macro and it expands to nothing.
        const QList<QPair<QString, QString>> roles = {
            { QStringLiteral("winner"), QStringLiteral("W") },
            { QStringLiteral("loser"),  QStringLiteral("L") },
            { QStringLiteral("save"),   QStringLiteral("S") }
        };

        QStringList ids;
        QVariantList order;
        for (const auto &sl : roles) {
            const QJsonObject p = d.value(sl.first).toObject();
            const int id = p.value(QStringLiteral("id")).toInt();
            if (id <= 0)
                continue;
            ids << QString::number(id);
            QVariantMap m;
            m[QStringLiteral("label")] = sl.second;
            m[QStringLiteral("id")]    = id;
            m[QStringLiteral("name")]  = p.value(QStringLiteral("fullName")).toString();
            order.append(m);
        }
        if (ids.isEmpty())
            return;

        const QString statsUrl = QStringLiteral(
            "%1/people?personIds=%2&hydrate=stats(group=pitching,type=season,season=%3)")
            .arg(QString::fromLatin1(kApi), ids.join(QLatin1Char(',')))
            .arg(easternNow().date().year());

        get(statsUrl, [this, order](const QJsonObject &po) {
            QHash<int, QVariantMap> stats;
            for (const QJsonValue &pv : po.value(QStringLiteral("people")).toArray()) {
                const QJsonObject p = pv.toObject();
                const QJsonArray sArr = p.value(QStringLiteral("stats")).toArray();
                if (sArr.isEmpty())
                    continue;
                const QJsonArray splits = sArr.first().toObject()
                                              .value(QStringLiteral("splits")).toArray();
                if (splits.isEmpty())
                    continue;
                const QJsonObject st = splits.first().toObject()
                                             .value(QStringLiteral("stat")).toObject();
                QVariantMap m;
                // "P Messick" is what fits; full names push the line too wide.
                m[QStringLiteral("name")] = p.value(QStringLiteral("initLastName")).toString();
                m[QStringLiteral("wins")]   = st.value(QStringLiteral("wins")).toInt();
                m[QStringLiteral("losses")] = st.value(QStringLiteral("losses")).toInt();
                m[QStringLiteral("saves")]  = st.value(QStringLiteral("saves")).toInt();
                m[QStringLiteral("era")]    = st.value(QStringLiteral("era")).toString();
                stats.insert(p.value(QStringLiteral("id")).toInt(), m);
            }

            QVariantList out;
            for (const QVariant &ov : order) {
                QVariantMap m = ov.toMap();
                const QVariantMap st = stats.value(m.value(QStringLiteral("id")).toInt());
                if (st.isEmpty())
                    continue;
                const QString label = m.value(QStringLiteral("label")).toString();
                m[QStringLiteral("name")] = st.value(QStringLiteral("name"));
                // A save is quoted by save count; a decision by won-lost.
                m[QStringLiteral("line")] = (label == QStringLiteral("S"))
                    ? QStringLiteral("%1").arg(st.value(QStringLiteral("saves")).toInt())
                    : QStringLiteral("%1-%2").arg(st.value(QStringLiteral("wins")).toInt())
                                             .arg(st.value(QStringLiteral("losses")).toInt());
                m[QStringLiteral("era")] = st.value(QStringLiteral("era")).toString() + QStringLiteral(" ERA");
                out.append(m);
            }
            m_state[QStringLiteral("pitchers")] = out;
            publish();
        });
    });
}

void GameFeed::requestSlate()
{
    // Every game today in one call. `hydrate=linescore` adds the inning for
    // games in progress; `team` adds abbreviations and the win/loss records.
    const QString url = QStringLiteral("%1/schedule?sportId=1&date=%2&hydrate=team,linescore")
                            .arg(QString::fromLatin1(kApi))
                            .arg(scheduleDate());

    get(url, [this](const QJsonObject &o) {
        const QJsonArray dates = o.value(QStringLiteral("dates")).toArray();
        const QJsonArray games = dates.isEmpty()
            ? QJsonArray()
            : dates.first().toObject().value(QStringLiteral("games")).toArray();

        QVariantList slate;
        for (const QJsonValue &v : games) {
            const QJsonObject g = v.toObject();
            const QJsonObject teams = g.value(QStringLiteral("teams")).toObject();
            const QJsonObject ls    = g.value(QStringLiteral("linescore")).toObject();
            const QJsonObject st    = g.value(QStringLiteral("status")).toObject();

            auto side = [&teams](const char *which) {
                return teams.value(QLatin1String(which)).toObject();
            };
            auto teamOf = [](const QJsonObject &s) {
                return s.value(QStringLiteral("team")).toObject();
            };
            auto recordOf = [](const QJsonObject &s) {
                const QJsonObject r = s.value(QStringLiteral("leagueRecord")).toObject();
                return QStringLiteral("%1-%2")
                    .arg(r.value(QStringLiteral("wins")).toInt())
                    .arg(r.value(QStringLiteral("losses")).toInt());
            };

            const QJsonObject away = side("away");
            const QJsonObject home = side("home");
            const QString abstract = st.value(QStringLiteral("abstractGameState")).toString();

            QVariantMap row;
            // Carried so a tapped row can populate the board immediately,
            // before the linescore for that game comes back.
            row[QStringLiteral("gamePk")]     = double(g.value(QStringLiteral("gamePk")).toDouble());
            row[QStringLiteral("awayName")]   = teamOf(away).value(QStringLiteral("teamName")).toString();
            row[QStringLiteral("homeName")]   = teamOf(home).value(QStringLiteral("teamName")).toString();
            row[QStringLiteral("venue")]      = g.value(QStringLiteral("venue")).toObject()
                                                 .value(QStringLiteral("name")).toString();
            row[QStringLiteral("statusText")] = st.value(QStringLiteral("detailedState")).toString();
            row[QStringLiteral("abstract")]   = abstract;
            row[QStringLiteral("awayAbbr")]   = teamOf(away).value(QStringLiteral("abbreviation")).toString();
            row[QStringLiteral("homeAbbr")]   = teamOf(home).value(QStringLiteral("abbreviation")).toString();
            row[QStringLiteral("awayId")]     = teamOf(away).value(QStringLiteral("id")).toInt();
            row[QStringLiteral("homeId")]     = teamOf(home).value(QStringLiteral("id")).toInt();
            row[QStringLiteral("awayRecord")] = recordOf(away);
            row[QStringLiteral("homeRecord")] = recordOf(home);
            row[QStringLiteral("isLive")]     = (abstract == QStringLiteral("Live"));
            row[QStringLiteral("isFinal")]    = (abstract == QStringLiteral("Final"));

            // Scores only exist once a game starts.
            const bool started = away.contains(QStringLiteral("score"));
            row[QStringLiteral("awayRuns")] = started
                ? QVariant(away.value(QStringLiteral("score")).toInt()) : QVariant(QString());
            row[QStringLiteral("homeRuns")] = started
                ? QVariant(home.value(QStringLiteral("score")).toInt()) : QVariant(QString());

            if (abstract == QStringLiteral("Live")) {
                const QString half = ls.value(QStringLiteral("inningState")).toString();
                QString h = QStringLiteral("TOP");
                if (half.startsWith(QStringLiteral("Bot")))      h = QStringLiteral("BOT");
                else if (half.startsWith(QStringLiteral("Mid"))) h = QStringLiteral("MID");
                else if (half.startsWith(QStringLiteral("End"))) h = QStringLiteral("END");
                row[QStringLiteral("note")] =
                    h + QStringLiteral(" ") + QString::number(ls.value(QStringLiteral("currentInning")).toInt());

                // The card draws its own diamond and outs, all from this call.
                const QJsonObject offense = ls.value(QStringLiteral("offense")).toObject();
                row[QStringLiteral("onFirst")]  = offense.contains(QStringLiteral("first"));
                row[QStringLiteral("onSecond")] = offense.contains(QStringLiteral("second"));
                row[QStringLiteral("onThird")]  = offense.contains(QStringLiteral("third"));
                row[QStringLiteral("outs")]     = ls.value(QStringLiteral("outs")).toInt();
            } else if (abstract == QStringLiteral("Final")) {
                // Extra innings are worth calling out; nine is unremarkable.
                const int inn = ls.value(QStringLiteral("currentInning")).toInt();
                row[QStringLiteral("note")] = inn > 9
                    ? QStringLiteral("FINAL/%1").arg(inn) : QStringLiteral("FINAL");
            } else {
                // Before first pitch, the useful fact is when it starts -- not
                // the word "SCHEDULED". gameDate is UTC ISO8601; show it in
                // Eastern, the zone the schedule is keyed to.
                const QDateTime start = QDateTime::fromString(
                    g.value(QStringLiteral("gameDate")).toString(), Qt::ISODate);
                const QTimeZone eastern("America/New_York");
                if (start.isValid() && eastern.isValid()) {
                    row[QStringLiteral("note")] =
                        start.toTimeZone(eastern).toString(QStringLiteral("h:mm AP"));
                } else if (start.isValid()) {
                    row[QStringLiteral("note")] =
                        start.addSecs(-5 * 60 * 60).toString(QStringLiteral("h:mm AP"));
                } else {
                    row[QStringLiteral("note")] =
                        st.value(QStringLiteral("detailedState")).toString().toUpper();
                }
            }

            slate.append(row);
        }

        // Label the list with the date it actually covers -- which is the
        // Eastern baseball day, not the tablet's UTC date.
        const QDate d = QDate::fromString(scheduleDate(), QStringLiteral("yyyy-MM-dd"));
        m_state[QStringLiteral("slateDate")] =
            QLocale::c().toString(d, QStringLiteral("dddd, MMMM d")).toUpper();

        m_state[QStringLiteral("loaded")] = true;
        m_state[QStringLiteral("slate")] = slate;
        publish();
    });
}

void GameFeed::requestLastPlay()
{
    // `fields` trims the response server-side. If the API ignores it the call
    // still works, it is just larger.
    const QString url =
        QStringLiteral("%1/game/%2/playByPlay?fields=currentPlay,result,description,"
                       "playEvents,isPitch,details,type,pitchData,startSpeed")
            .arg(QString::fromLatin1(kApi)).arg(m_gamePk);

    get(url, [this](const QJsonObject &o) {
        const QJsonObject cur = o.value(QStringLiteral("currentPlay")).toObject();

        m_state[QStringLiteral("lastPlay")] =
            cur.value(QStringLiteral("result")).toObject()
               .value(QStringLiteral("description")).toString();

        // The pitch that was just thrown: "SPLITTER · 83.4 · FOUL TIP".
        // Between batters currentPlay carries no events, so hold the previous
        // line rather than blinking it away every half inning.
        const QJsonArray events = cur.value(QStringLiteral("playEvents")).toArray();
        for (int i = events.size() - 1; i >= 0; --i) {
            const QJsonObject e = events.at(i).toObject();
            if (!e.value(QStringLiteral("isPitch")).toBool())
                continue;

            const QJsonObject det = e.value(QStringLiteral("details")).toObject();
            QStringList bits;
            const QString type = det.value(QStringLiteral("type")).toObject()
                                    .value(QStringLiteral("description")).toString();
            if (!type.isEmpty())
                bits << type.toUpper();

            const double mph = e.value(QStringLiteral("pitchData")).toObject()
                                .value(QStringLiteral("startSpeed")).toDouble();
            if (mph > 0)
                bits << QStringLiteral("%1").arg(mph, 0, 'f', 1);

            const QString call = det.value(QStringLiteral("description")).toString();
            if (!call.isEmpty())
                bits << call.toUpper();

            if (!bits.isEmpty())
                m_state[QStringLiteral("lastPitch")] = bits.join(QStringLiteral("  ·  "));
            break;
        }

        publish();
    });
}

void GameFeed::publish()
{
    m_state[QStringLiteral("updatedAt")] =
        easternNow().time().toString(QStringLiteral("h:mm:ss AP"));
    emit stateChanged();
}

void GameFeed::loadDemoGame(const QString &demoState)
{
    auto S = [](const char *s) { return QString::fromUtf8(s); };

    m_state[QStringLiteral("loaded")]        = true;
    m_state[QStringLiteral("hasGame")]       = true;
    m_state[QStringLiteral("abstractState")] = S("Live");
    m_state[QStringLiteral("statusText")]    = S("In Progress");
    m_state[QStringLiteral("venue")]         = S("Progressive Field");

    m_state[QStringLiteral("awayName")] = S("White Sox");
    m_state[QStringLiteral("homeName")] = S("Guardians");
    m_state[QStringLiteral("awayAbbr")] = S("CWS");
    m_state[QStringLiteral("homeAbbr")] = S("CLE");
    m_state[QStringLiteral("awayId")]   = 145;
    m_state[QStringLiteral("homeId")]   = 114;

    m_state[QStringLiteral("awayRuns")]   = 3;
    m_state[QStringLiteral("homeRuns")]   = 6;
    m_state[QStringLiteral("awayHits")]   = 9;
    m_state[QStringLiteral("homeHits")]   = 11;
    m_state[QStringLiteral("awayErrors")] = 1;
    m_state[QStringLiteral("homeErrors")] = 0;

    m_state[QStringLiteral("inningOrdinal")] = S("7th");
    m_state[QStringLiteral("inningState")]   = S("Bottom");
    m_state[QStringLiteral("isTopInning")]   = false;

    m_state[QStringLiteral("balls")]   = 2;
    m_state[QStringLiteral("strikes")] = 1;
    m_state[QStringLiteral("outs")]    = 2;

    m_state[QStringLiteral("onFirst")]  = true;
    m_state[QStringLiteral("onSecond")] = false;
    m_state[QStringLiteral("onThird")]  = true;

    m_state[QStringLiteral("batter")]  = S("Steven Kwan");
    m_state[QStringLiteral("pitcher")] = S("Garrett Crochet");
    m_state[QStringLiteral("lastPlay")] =
        S("Jose Ramirez doubles (31) on a sharp line drive to right fielder, "
          "Angel Genao scores. Brayan Rocchio to 3rd.");

    const int away[9] = {0, 1, 0, 0, 2, 0, 0, -1, -1};
    const int home[9] = {0, 0, 2, 1, 0, 0, 3, -1, -1};
    QVariantList innings;
    for (int i = 0; i < 9; ++i) {
        QVariantMap row;
        row[QStringLiteral("num")]  = i + 1;
        row[QStringLiteral("away")] = away[i] < 0 ? QVariant(QStringLiteral("-"))
                                                  : QVariant(away[i]);
        row[QStringLiteral("home")] = home[i] < 0 ? QVariant(QStringLiteral("-"))
                                                  : QVariant(home[i]);
        innings.append(row);
    }
    m_state[QStringLiteral("innings")] = innings;

    if (demoState == QStringLiteral("none")) {
        m_state[QStringLiteral("hasGame")]       = false;
        m_state[QStringLiteral("abstractState")] = S("Preview");
        m_state[QStringLiteral("statusText")]    = S("No game scheduled today");
    } else if (demoState == QStringLiteral("pregame")) {
        m_state[QStringLiteral("abstractState")] = S("Preview");
        m_state[QStringLiteral("statusText")]    = S("Today 1:10 PM \u00b7 Bibee vs. Cannon");
        m_state[QStringLiteral("awayRuns")]      = 0;
        m_state[QStringLiteral("homeRuns")]      = 0;
    } else if (demoState == QStringLiteral("final")) {
        m_state[QStringLiteral("abstractState")] = S("Final");
        m_state[QStringLiteral("statusText")]    = S("Final");
        m_state[QStringLiteral("lastPlay")]      =
            S("Cade Smith strikes out Chase Meidroth swinging.");
    }

    publish();
}
