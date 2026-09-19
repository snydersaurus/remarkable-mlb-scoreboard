#pragma once

#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>

/*
 * Team logos, fetched once and cached on disk as greyscale SVG.
 *
 * MLB serves small clean SVGs (~1.4KB) with plain fill attributes and no CSS.
 * Flattening every fill to black would destroy logos that rely on internal
 * contrast -- the Guardians cap is a navy shape with a red C inside it, and
 * both are dark. So each fill is mapped to its luma instead, which keeps the
 * shapes apart while staying inside the black-on-white design.
 */
class LogoStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit LogoStore(QObject *parent = nullptr);

    int revision() const { return m_revision; }

    // Returns a file:// url once cached, or an empty string while fetching.
    // Requesting an uncached id starts the download.
    Q_INVOKABLE QString logoFor(int teamId);

signals:
    void revisionChanged();

private:
    void fetch(int teamId);
    static QByteArray toGreyscale(const QByteArray &svg);

    QNetworkAccessManager m_net;
    QHash<int, QString> m_ready;
    QHash<int, bool> m_inFlight;
    QString m_cacheDir;
    int m_revision = 0;
};
