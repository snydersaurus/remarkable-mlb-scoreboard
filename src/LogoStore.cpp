#include "LogoStore.h"

#include <QDir>
#include <QFile>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {
const char *kLogoUrl = "https://www.mlbstatic.com/team-logos/%1.svg";
}

LogoStore::LogoStore(QObject *parent) : QObject(parent)
{
    // /home/root survives firmware updates; the XDG cache dir may not exist.
    m_cacheDir = QDir::homePath() + QStringLiteral("/.cache/scoreboard-logos");
    QDir().mkpath(m_cacheDir);

    // Adopt anything cached from a previous run.
    const QStringList existing = QDir(m_cacheDir).entryList(QStringList() << "*.svg", QDir::Files);
    for (const QString &f : existing) {
        const int id = QFileInfo(f).baseName().toInt();
        if (id > 0)
            m_ready.insert(id, QUrl::fromLocalFile(m_cacheDir + "/" + f).toString());
    }
}

QString LogoStore::logoFor(int teamId)
{
    if (teamId <= 0)
        return QString();
    if (m_ready.contains(teamId))
        return m_ready.value(teamId);
    if (!m_inFlight.value(teamId, false))
        fetch(teamId);
    return QString();
}

void LogoStore::fetch(int teamId)
{
    m_inFlight[teamId] = true;

    QNetworkRequest req{QUrl(QString::fromLatin1(kLogoUrl).arg(teamId))};
    req.setRawHeader("User-Agent", "rmpp-scoreboard/1.0");
    QNetworkReply *reply = m_net.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, teamId]() {
        reply->deleteLater();
        m_inFlight[teamId] = false;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("logo %d failed: %s", teamId, qPrintable(reply->errorString()));
            return;
        }

        const QByteArray grey = toGreyscale(reply->readAll());
        const QString path = m_cacheDir + QStringLiteral("/%1.svg").arg(teamId);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            qWarning("cannot write %s", qPrintable(path));
            return;
        }
        f.write(grey);
        f.close();

        m_ready.insert(teamId, QUrl::fromLocalFile(path).toString());
        ++m_revision;
        emit revisionChanged();
    });
}

QByteArray LogoStore::toGreyscale(const QByteArray &svg)
{
    QString s = QString::fromUtf8(svg);

    // Colours appear in two shapes: as attributes (fill="#abc") on most logos,
    // and inside a <style> block as CSS (.cls-1{fill:#ffce34}) on a minority --
    // one of the thirty at the time of writing. Missing the CSS form leaves
    // that logo in full colour, so handle both.
    auto grey = [](QString hex) {
        if (hex.size() == 3) {
            QString expanded;
            for (const QChar &c : hex) { expanded += c; expanded += c; }
            hex = expanded;
        }
        const int r = hex.mid(0, 2).toInt(nullptr, 16);
        const int g = hex.mid(2, 2).toInt(nullptr, 16);
        const int b = hex.mid(4, 2).toInt(nullptr, 16);

        // Rec.601 luma, then split into two cases. Near-white is almost always
        // knock-out space -- the inner disc of the Cubs roundel, say -- so it
        // stays white. Everything else is ink and gets darkened enough to read
        // on a white page: the Pirates' gold is luma 203, which left alone is
        // invisible at this size.
        int y = qRound(0.299 * r + 0.587 * g + 0.114 * b);
        y = (y >= 245) ? 255 : qMin(y, 150);
        return QString::number(y, 16).rightJustified(2, QLatin1Char('0')).repeated(3);
    };

    auto rewrite = [&grey](const QString &in, const QString &pattern,
                           const QString &fmt) {
        const QRegularExpression re(pattern);
        QString out;
        out.reserve(in.size());
        int last = 0;
        auto it = re.globalMatch(in);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            out += in.mid(last, m.capturedStart() - last);
            out += QString(fmt).arg(m.captured(1)).arg(grey(m.captured(2)));
            last = m.capturedEnd();
        }
        out += in.mid(last);
        return out;
    };

    // The 6-digit branch must be tried first: with {3} leading, "#ffce34"
    // matches as "ffc" wherever no delimiter forces backtracking, which turned
    // the Pirates' gold into white.
    s = rewrite(s, QStringLiteral("(fill|stroke)=\"#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})\""),
                   QStringLiteral("%1=\"#%2\""));
    s = rewrite(s, QStringLiteral("(fill|stroke)\\s*:\\s*#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![0-9a-fA-F])"),
                   QStringLiteral("%1:#%2"));
    return s.toUtf8();
}
