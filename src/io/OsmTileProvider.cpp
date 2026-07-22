#include "io/OsmTileProvider.hpp"
#include "util/Logger.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>

OsmTileProvider::OsmTileProvider(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    auto* diskCache = new QNetworkDiskCache(this);
    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/osm";
    QDir().mkpath(cacheDir);
    diskCache->setCacheDirectory(cacheDir);
    diskCache->setMaximumCacheSize(512LL * 1024 * 1024);
    m_nam->setCache(diskCache);

    connect(m_nam, &QNetworkAccessManager::finished,
            this,  &OsmTileProvider::onReplyFinished);
}

void OsmTileProvider::setUrlTemplate(const QString& url)
{
    m_url_template = url;
    m_h2_disabled  = false;   // reset: new server may support HTTP/2
}

QString OsmTileProvider::tileUrl(int z, int x, int y) const
{
    QString url = m_url_template;
    url.replace("{z}", QString::number(z));
    url.replace("{x}", QString::number(x));
    url.replace("{y}", QString::number(y));
    return url;
}

QImage OsmTileProvider::requestTile(int z, int x, int y)
{
    OsmTileKey key{z, x, y};

    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    if (m_pending.count(key)) return {};
    if (m_url_template.isEmpty()) return {};

    QNetworkRequest req(QUrl(tileUrl(z, x, y)));
    if (m_h2_disabled)
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setRawHeader("User-Agent",
                     "FlashViewer/0.1 (+https://github.com/san-aria/flashviewer)");
    req.setRawHeader("Accept", "image/png,image/*;q=0.9,*/*;q=0.5");
    req.setTransferTimeout(15000);

    QNetworkReply* reply = m_nam->get(req);
    m_reply_keys[reply] = key;
    m_pending[key] = true;
    return {};
}

QImage OsmTileProvider::cachedTile(int z, int x, int y) const
{
    auto it = m_cache.find({z, x, y});
    return (it != m_cache.end()) ? it->second : QImage{};
}

void OsmTileProvider::clearCache()
{
    // Collect pointers before clearing — abort() fires onReplyFinished()
    // synchronously (direct connection), which would erase from m_reply_keys
    // and invalidate the active range-for iterator.
    std::vector<QNetworkReply*> inflight;
    inflight.reserve(m_reply_keys.size());
    for (auto& [reply, key] : m_reply_keys)
        inflight.push_back(reply);

    m_reply_keys.clear();   // clear first — onReplyFinished() hits the early-return guard
    m_pending.clear();
    m_cache.clear();

    for (auto* r : inflight)
        r->abort();

    m_nam->clearConnectionCache();
}

void OsmTileProvider::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    auto keyIt = m_reply_keys.find(reply);
    if (keyIt == m_reply_keys.end()) return;
    OsmTileKey key = keyIt->second;
    m_reply_keys.erase(keyIt);
    m_pending.erase(key);

    if (reply->error() != QNetworkReply::NoError) {
        bool h2_was_used = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()
                         || reply->errorString().contains("stream", Qt::CaseInsensitive);
        bool is_content_err = (reply->error() >= QNetworkReply::ContentAccessDenied &&
                                reply->error() <= QNetworkReply::UnknownContentError);
        if (!m_h2_disabled && h2_was_used && !is_content_err) {
            m_h2_disabled = true;
            m_nam->clearConnectionCache();
            FV_INFO("OsmTile: HTTP/2 rejected by server, retrying with HTTP/1.1");
            return;   // renderer retries this tile next frame with HTTP/1.1
        }
        FV_WARN("OsmTile z={} x={} y={}: {}",
                key.z, key.x, key.y, reply->errorString().toStdString());
        return;
    }

    QImage img;
    if (!img.loadFromData(reply->readAll())) {
        FV_WARN("OsmTile z={} x={} y={}: PNG decode failed", key.z, key.x, key.y);
        return;
    }
    m_cache[key] = img.convertToFormat(QImage::Format_RGBA8888);
    emit tileReady(key.z, key.x, key.y);
}
