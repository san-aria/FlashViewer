#include <catch2/catch_test_macros.hpp>
#include "io/UrlGuard.hpp"

#include <QStringList>

// A mock resolver mapping a host to a fixed IP set, so SSRF range checks run with
// no real DNS (TC-SEC-03). Any host not in the map "does not resolve".
static UrlGuard::HostResolver mockResolver(QString host, QStringList ips) {
    return [h = std::move(host), ipset = std::move(ips)](const QString& query) {
        return (query == h) ? ipset : QStringList{};
    };
}

TEST_CASE("UrlGuard: scheme allowlist (FR-SEC-2)", "[TC-SEC-02]") {
    REQUIRE(UrlGuard::isAllowedScheme("http://example.com/a.tif"));
    REQUIRE(UrlGuard::isAllowedScheme("https://example.com/a.tif"));
    REQUIRE(UrlGuard::isAllowedScheme("/vsicurl/https://example.com/a.tif"));
    REQUIRE(UrlGuard::isAllowedScheme("/vsis3/bucket/key.tif"));
    REQUIRE(UrlGuard::isAllowedScheme("/vsigs/bucket/key.tif"));

    REQUIRE_FALSE(UrlGuard::isAllowedScheme("ftp://example.com/a.tif"));
    REQUIRE_FALSE(UrlGuard::isAllowedScheme("file:///etc/passwd"));
    REQUIRE_FALSE(UrlGuard::isAllowedScheme("/vsimem/x.tif"));
    REQUIRE_FALSE(UrlGuard::isAllowedScheme("gopher://example.com"));

    // check() rejects a disallowed scheme before any resolution.
    auto r = UrlGuard::check("file:///etc/passwd", mockResolver("x", {"1.2.3.4"}));
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("UrlGuard: blocked address ranges (FR-SEC-3)", "[TC-SEC-03]") {
    // Loopback / link-local / metadata / private → blocked.
    REQUIRE(UrlGuard::isBlockedAddress("127.0.0.1"));
    REQUIRE(UrlGuard::isBlockedAddress("127.9.9.9"));
    REQUIRE(UrlGuard::isBlockedAddress("169.254.169.254"));   // cloud metadata
    REQUIRE(UrlGuard::isBlockedAddress("169.254.1.1"));
    REQUIRE(UrlGuard::isBlockedAddress("10.0.0.5"));
    REQUIRE(UrlGuard::isBlockedAddress("172.16.0.1"));
    REQUIRE(UrlGuard::isBlockedAddress("172.31.255.255"));
    REQUIRE(UrlGuard::isBlockedAddress("192.168.1.1"));
    REQUIRE(UrlGuard::isBlockedAddress("0.0.0.0"));
    REQUIRE(UrlGuard::isBlockedAddress("::1"));
    REQUIRE(UrlGuard::isBlockedAddress("fe80::1"));           // link-local v6
    REQUIRE(UrlGuard::isBlockedAddress("fc00::1"));           // unique-local v6

    // Public addresses → allowed.
    REQUIRE_FALSE(UrlGuard::isBlockedAddress("8.8.8.8"));
    REQUIRE_FALSE(UrlGuard::isBlockedAddress("172.32.0.1"));  // just outside 172.16/12
    REQUIRE_FALSE(UrlGuard::isBlockedAddress("93.184.216.34"));
}

TEST_CASE("UrlGuard: check blocks SSRF via resolved host (FR-SEC-3)", "[TC-SEC-03]") {
    // Host resolves into a private/metadata range → rejected.
    auto meta = UrlGuard::check("https://evil.example/a.tif",
                                mockResolver("evil.example", {"169.254.169.254"}));
    REQUIRE_FALSE(meta.ok);

    auto priv = UrlGuard::check("https://evil.example/a.tif",
                                mockResolver("evil.example", {"10.1.2.3"}));
    REQUIRE_FALSE(priv.ok);

    // Host resolves to a public IP → allowed.
    auto ok = UrlGuard::check("https://good.example/a.tif",
                              mockResolver("good.example", {"93.184.216.34"}));
    REQUIRE(ok.ok);

    // Any resolved IP in the blocked set poisons the whole host (rebind defense).
    auto mixed = UrlGuard::check("https://mixed.example/a.tif",
                                 mockResolver("mixed.example", {"8.8.8.8", "127.0.0.1"}));
    REQUIRE_FALSE(mixed.ok);

    // Unresolvable host → rejected (fail closed).
    auto nores = UrlGuard::check("https://ghost.example/a.tif",
                                 mockResolver("other", {"8.8.8.8"}));
    REQUIRE_FALSE(nores.ok);
}

TEST_CASE("UrlGuard: IP-literal host checked without DNS", "[TC-SEC-03]") {
    // A resolver that would allow anything must NOT be consulted for a literal IP.
    auto allowAll = [](const QString&) { return QStringList{"8.8.8.8"}; };
    REQUIRE_FALSE(UrlGuard::check("http://127.0.0.1/x.tif", allowAll).ok);
    REQUIRE(UrlGuard::check("http://93.184.216.34/x.tif", allowAll).ok);
}

TEST_CASE("UrlGuard: hostFromUrl strips VSI wrappers and lowercases") {
    REQUIRE(UrlGuard::hostFromUrl("https://Example.COM/a.tif") == "example.com");
    REQUIRE(UrlGuard::hostFromUrl("/vsicurl/https://Example.com/a.tif") == "example.com");
    // A /vsis3/ path has no resolvable host → empty (allowed by scheme alone).
    REQUIRE(UrlGuard::hostFromUrl("/vsis3/bucket/key.tif").isEmpty());
}
