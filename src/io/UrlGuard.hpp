#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include <string>

// Pre-fetch SSRF guard for user-supplied remote URLs (SDD §12.2, FR-SEC-1/2/3),
// following OWASP SSRF-prevention guidance: strict scheme allowlist, host
// normalization, DNS resolution, and blocking of loopback / link-local / private /
// cloud-metadata address ranges. Invoked by CloudReader::open before the URL is
// handed to GDAL, and by the File → Open URL entry point for early rejection.
class UrlGuard {
public:
    struct Result {
        bool    ok{false};
        QString reason;   // human-readable rejection reason (empty when ok)
    };

    // host → resolved IP literals (empty list = resolution failure). Injectable so
    // tests supply a mock DNS map without real network I/O (TC-SEC-03).
    using HostResolver = std::function<QStringList(const QString& host)>;

    // Full pre-fetch check (FR-SEC-1/2/3).
    static Result check(const std::string& url) { return check(url, defaultResolver()); }
    static Result check(const std::string& url, const HostResolver& resolver);

    // Predicates exposed for unit testing:
    static bool    isAllowedScheme(const QString& url);         // FR-SEC-2
    static bool    isBlockedAddress(const QString& ip_literal); // FR-SEC-3 (one IP)
    static QString hostFromUrl(const QString& url);             // normalized host

    static HostResolver defaultResolver();
};
