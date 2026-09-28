/*
 * AppLoad backend for the scoreboard.
 *
 * The frontend is QML loaded into xochitl itself, so it cannot fetch anything:
 * it inherits xochitl's OpenSSL policy, which blocks the RSA/TLS1.2 handshake
 * statsapi.mlb.com needs, and OPENSSL_CONF cannot be set for xochitl without
 * changing device-wide TLS. Doing the network here is what makes the split
 * work -- this is a separate process with its own environment.
 *
 * Everything is pushed as one JSON blob whenever it changes; the frontend is a
 * pure view. Logos are written to a cache directory and the QML loads them by
 * file path, so no image data crosses the socket.
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include "AppLoadLink.h"
#include "GameFeed.h"
#include "LogoStore.h"

namespace {

// Backend -> frontend
constexpr quint32 MsgState = 101;
// Frontend -> backend
constexpr quint32 MsgHello       = 1;   // frontend ready, send everything
constexpr quint32 MsgShowGame    = 2;   // contents: gamePk
constexpr quint32 MsgShowTeam    = 3;   // back to the followed team
constexpr quint32 MsgGeometry    = 4;   // frontend reporting its window size
constexpr quint32 MsgSetTeam     = 5;   // contents: teamId to follow
constexpr quint32 MsgRefresh     = 6;   // the app is back on screen; refetch now

// Deliberately outside the app directory: a package upgrade replaces
// /home/root/xovi/exthome/appload/<app>/ wholesale, and a reinstall should not
// forget which team you follow.
const char *kSettingsPath = "/home/root/.config/scoreboard/settings.json";

// AppLoad reads icon.png once, when xochitl starts, so a change only shows up
// after a restart. It has to live inside the app directory -- that is where
// AppLoad looks -- but the directory name is chosen by whoever installs the
// app, so derive it rather than hardcoding one. The backend runs as
// <appdir>/backend/entry, so the app directory is two levels up.
QString iconPath()
{
    const QString exe = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    if (exe.isEmpty())
        return QString();
    return QFileInfo(exe).dir().filePath(QStringLiteral("../icon.png"));
}

int loadFollowedTeam(int fallback)
{
    QFile f(QString::fromLatin1(kSettingsPath));
    if (!f.open(QIODevice::ReadOnly))
        return fallback;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const int id = o.value(QStringLiteral("teamId")).toInt();
    return id > 0 ? id : fallback;
}

void saveFollowedTeam(int teamId)
{
    QDir().mkpath(QFileInfo(QString::fromLatin1(kSettingsPath)).absolutePath());
    QFile f(QString::fromLatin1(kSettingsPath));
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("settings: cannot write %s", kSettingsPath);
        return;
    }
    QJsonObject o;
    o[QStringLiteral("teamId")] = teamId;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// The tablet autosleeps aggressively -- deep suspend roughly 40 seconds after
// the last touch -- and nothing runs while it is down: no timers, no network.
// That is why a live score would sit frozen with a stale "updated" time.
//
// Hold an Android-style wakelock so the poll keeps running, but ONLY while a
// game is actually in progress. A scoreboard that pins the device awake all day
// would be a battery bug; one that does it during a live game is the point.
void setWakeLock(bool wanted)
{
    static bool held = false;
    if (wanted == held)
        return;

    QFile f(wanted ? QStringLiteral("/sys/power/wake_lock")
                   : QStringLiteral("/sys/power/wake_unlock"));
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("wakelock: cannot open %s", qPrintable(f.fileName()));
        return;
    }
    f.write("guardians_scoreboard");
    f.close();
    held = wanted;
    qInfo("wakelock: %s", wanted ? "held (live game)" : "released");
}

// Is anything actually being played right now?
bool anythingLive(const QVariantMap &state)
{
    if (state.value(QStringLiteral("abstractState")).toString() == QStringLiteral("Live"))
        return true;
    for (const QVariant &row : state.value(QStringLiteral("slate")).toList())
        if (row.toMap().value(QStringLiteral("isLive")).toBool())
            return true;
    return false;
}

// Replace the launcher icon with the followed team's mark. MLB serves these as
// PNG at an arbitrary size, so nothing has to be rasterised here.
//
// The app ships a neutral icon; this only writes a club's logo once someone
// actively picks that club.
void writeLauncherIcon(QNetworkAccessManager *net, int teamId)
{
    if (teamId <= 0)
        return;

    const QUrl url(QStringLiteral(
        "https://midfield.mlbstatic.com/v1/team/%1/spots/600").arg(teamId));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "rmpp-scoreboard/1.0");

    QNetworkReply *reply = net->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("icon: fetch failed: %s", qPrintable(reply->errorString()));
            return;
        }
        const QString path = iconPath();
        QFile f(path);
        if (path.isEmpty() || !f.open(QIODevice::WriteOnly)) {
            qWarning("icon: cannot write %s", qPrintable(path));
            return;
        }
        f.write(reply->readAll());
        qInfo("icon: updated (visible after the tablet restarts)");
    });
}

// Make sure every team on screen has its logo on disk for the frontend to load.
void ensureLogos(LogoStore *logos, const QVariantMap &state)
{
    const int ids[] = { state.value(QStringLiteral("awayId")).toInt(),
                        state.value(QStringLiteral("homeId")).toInt() };
    for (int id : ids)
        if (id > 0)
            logos->logoFor(id);

    for (const QVariant &row : state.value(QStringLiteral("slate")).toList()) {
        const QVariantMap m = row.toMap();
        for (const char *k : { "awayId", "homeId" }) {
            const int id = m.value(QLatin1String(k)).toInt();
            if (id > 0)
                logos->logoFor(id);
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // The device restricts TLS 1.2 to ECDHE-ECDSA suites (SOG-IS, for EU-RED),
    // and statsapi.mlb.com serves an RSA cert without TLS 1.3 -- no overlap, so
    // the handshake dies and everything reads OFFLINE. AppLoad launches this
    // backend with a bare environment, so set the per-process OpenSSL config
    // here rather than in the manifest, which would cost a xochitl restart.
    // Must happen before anything touches OpenSSL.
    qputenv("OPENSSL_CONF", "/home/root/openssl-scoreboard.cnf");

    QCoreApplication app(argc, argv);

    if (argc < 2) {
        qWarning("usage: entry <appload-socket>   (AppLoad passes this)");
        return 2;
    }

    AppLoadLink link;
    if (!link.connectTo(QString::fromLocal8Bit(argv[1])))
        return 1;

    LogoStore logos;
    QNetworkAccessManager net;

    // Distinguish "nobody has picked yet" from "somebody picked Cleveland".
    // Defaulting straight to 114 made those identical, so a first-time user
    // silently got someone else's team and no reason to think it was a choice.
    const int savedTeam = loadFollowedTeam(0);
    bool teamChosen = savedTeam > 0;

    // Still follow a team while unchosen, so the board has something on it
    // behind the picker rather than reading "no game".
    GameFeed feed(teamChosen ? savedTeam : 114);
    feed.requestTeamList();


    auto push = [&]() {
        QVariantMap state = feed.state();
        state[QStringLiteral("teamChosen")] = teamChosen;
        ensureLogos(&logos, state);
        // Wakelock is OFF by default. Holding one during a live game keeps the
        // score ticking, but if the app is left open the tablet never suspends
        // and the battery goes with it -- hours, for a scoreboard on a desk.
        // Not worth it: after a suspend the poll resumes and the board is
        // current within one cycle, and reopening refetches immediately.
        // Set SCOREBOARD_STAY_AWAKE=1 to opt in.
        static const bool stayAwake = qEnvironmentVariableIntValue("SCOREBOARD_STAY_AWAKE") == 1;
        setWakeLock(stayAwake && anythingLive(state));
        const QByteArray json =
            QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact);
        link.send(MsgState, json);
    };

    QObject::connect(&feed, &GameFeed::stateChanged, &app, push);

    // A logo landing changes nothing in the state map, but the frontend needs
    // to re-check the files, so nudge it.
    QObject::connect(&logos, &LogoStore::revisionChanged, &app, push);

    QObject::connect(&link, &AppLoadLink::messageReceived, &app,
                     [&](quint32 type, const QByteArray &payload) {
        switch (type) {
        case AppLoadLink::MsgNewCoordinator:
        case MsgHello:
            // A frontend attached (or re-attached) -- it has no state yet.
            // Push what we have so the screen is never blank, then refetch:
            // after a suspend the cached state can be minutes stale.
            push();
            feed.refresh();
            break;
        case MsgShowGame: {
            const qint64 pk = payload.trimmed().toLongLong();
            if (pk > 0) {
                // The slate row for this game carries the names the board needs.
                QVariantMap info;
                for (const QVariant &row : feed.state().value(QStringLiteral("slate")).toList()) {
                    const QVariantMap m = row.toMap();
                    if (qint64(m.value(QStringLiteral("gamePk")).toDouble()) == pk) {
                        info = m;
                        break;
                    }
                }
                feed.showGame(double(pk), info);
            }
            break;
        }
        case MsgShowTeam:
            feed.showTeamGame();
            break;
        case MsgRefresh:
            feed.refresh();
            break;
        case MsgGeometry:
            qInfo("frontend window: %s", payload.constData());
            break;
        case MsgSetTeam: {
            const int id = payload.trimmed().toInt();
            if (id <= 0)
                break;
            // Act on the pick even when it matches the team already being
            // followed. Guarding the whole block on "did it change" meant
            // choosing the default team silently did nothing -- no saved
            // setting, no icon -- which just looks broken.
            qInfo("following team %d", id);
            teamChosen = true;
            saveFollowedTeam(id);
            writeLauncherIcon(&net, id);
            feed.setTeam(id);   // itself a no-op if unchanged
            break;
        }
        case AppLoadLink::MsgTerminate:
            qInfo("backend: AppLoad asked us to terminate");
            app.quit();
            break;
        default:
            break;
        }
    });

    QObject::connect(&link, &AppLoadLink::disconnected, &app, [&]() {
        qInfo("backend: link closed, exiting");
        setWakeLock(false);   // never leave the tablet pinned awake
        // The frontend went away. AppLoad keeps backends alive by design, but
        // there is nothing to serve and no reason to keep polling MLB.
        app.quit();
    });

    push();
    return app.exec();
}
