#include "io/UrlGuard.hpp"

#include <QUrl>
#include <QHostAddress>
#include <QHostInfo>
#include <QAbstractSocket>

namespace {

// VSI wrappers that may prefix a real URL. Order matters: /vsicurl/ wraps http(s).
const char* const kVsiPrefixes[] = {"/vsicurl/", "/vsis3/", "/vsigs/"};

QString stripVsi(const QString& url) {
    for (const char* p : kVsiPrefixes) {
        if (url.startsWith(QLatin1String(p)))
            return url.mid(static_cast<int>(qstrlen(p)));
    }
    return url;
}

} // namespace

bool UrlGuard::isAllowedScheme(const QString& url) {
    // FR-SEC-2: only http/https and the three cloud VSI wrappers.
    return url.startsWith(QLatin1String("http://"),  Qt::CaseInsensitive)
        || url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
        || url.startsWith(QLatin1String("/vsicurl/"))
        || url.startsWith(QLatin1String("/vsis3/"))
        || url.startsWith(QLatin1String("/vsigs/"));
}

QString UrlGuard::hostFromUrl(const QString& url) {
    // Strip a leading VSI wrapper, then parse. For /vsis3/bucket/key (no scheme)
    // QUrl yields an empty host → treated as "no resolvable host" by check().
    QUrl q(stripVsi(url));
    return q.host().toLower();
}

bool UrlGuard::isBlockedAddress(const QString& ip_literal) {
    QHostAddress a(ip_literal);
    if (a.isNull()) return true;                 // unparseable → fail safe (block)
    if (a.isLoopback())  return true;            // 127.0.0.0/8, ::1
    if (a.isLinkLocal()) return true;            // 169.254.0.0/16 (incl. metadata), fe80::/10
    if (a.isMulticast()) return true;

    if (a.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v  = a.toIPv4Address();
        const quint8  o1 = static_cast<quint8>((v >> 24) & 0xFF);
        const quint8  o2 = static_cast<quint8>((v >> 16) & 0xFF);
        if (o1 == 0)                        return true;  // 0.0.0.0/8 (this host)
        if (o1 == 10)                       return true;  // 10.0.0.0/8
        if (o1 == 127)                      return true;  // 127.0.0.0/8
        if (o1 == 169 && o2 == 254)         return true;  // 169.254/16 (metadata)
        if (o1 == 172 && o2 >= 16 && o2 <= 31) return true;  // 172.16/12
        if (o1 == 192 && o2 == 168)         return true;  // 192.168/16
    } else {
        if (a.isUniqueLocalUnicast()) return true;  // fc00::/7
    }
    return false;
}

UrlGuard::HostResolver UrlGuard::defaultResolver() {
    // Synchronous DNS — CloudReader::open runs off the UI thread, so blocking here
    // is fine. Re-invoked on the resolved host for redirect hardening at the GDAL
    // layer (bounded redirects; see CloudReader::configureGdal).
    return [](const QString& host) {
        QStringList out;
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress& a : info.addresses())
            out << a.toString();
        return out;
    };
}

UrlGuard::Result UrlGuard::check(const std::string& url, const HostResolver& resolver) {
    const QString u = QString::fromStdString(url);

    if (!isAllowedScheme(u))
        return {false, QStringLiteral("URL scheme not allowed: %1").arg(u)};

    const QString host = hostFromUrl(u);
    if (host.isEmpty())
        return {true, {}};   // scheme-allowlisted VSI path with no resolvable host

    // Host may already be an IP literal — check it directly (no DNS).
    QHostAddress literal(host);
    if (!literal.isNull()) {
        if (isBlockedAddress(host))
            return {false, QStringLiteral("URL host is a blocked address: %1").arg(host)};
        return {true, {}};
    }

    const QStringList ips = resolver(host);
    if (ips.isEmpty())
        return {false, QStringLiteral("URL host did not resolve: %1").arg(host)};
    for (const QString& ip : ips) {
        if (isBlockedAddress(ip))
            return {false, QStringLiteral("URL host resolves to a blocked range: %1 -> %2")
                               .arg(host, ip)};
    }
    return {true, {}};
}
