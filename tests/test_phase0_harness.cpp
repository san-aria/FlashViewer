// Phase 0 — test-infrastructure self-tests.
//
// Verifies the shared harnesses (FixtureFactory, GdalOracle, MockNam,
// GlTestHarness) before any feature tests rely on them. These are the
// foundation for the docs/TEST_SPEC.md TC-* catalog.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fixtures/FixtureFactory.hpp"
#include "harness/GdalOracle.hpp"
#include "harness/MockNam.hpp"
#include "harness/GlTestHarness.hpp"

#include "io/RasterDataset.hpp"

#include <QByteArray>
#include <QBuffer>
#include <QImage>
#include <QColor>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

#include <set>
#include <cmath>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---- helpers ----------------------------------------------------------------

// Drive the event loop until `reply` finishes or a timeout elapses.
static void waitForReply(QNetworkReply* reply, int timeout_ms = 3000) {
    if (reply->isFinished()) return;
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    loop.exec();
}

static QByteArray makePng1x1(QColor c) {
    QImage img(1, 1, QImage::Format_RGBA8888);
    img.fill(c);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

// ---- FixtureFactory + RasterDataset round-trips -----------------------------

TEST_CASE("Harness: gradient fixture round-trips through RasterDataset", "[harness][fixture]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(16, 16);

    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);
    REQUIRE(ds->width()  == 16);
    REQUIRE(ds->height() == 16);
    REQUIRE(ds->bandCount() == 1);

    // value(col,row) == col + row*width
    auto buf = ds->readRegion(0, 0, 16, 16, 16, 16, {1});
    REQUIRE(buf.isValid());
    REQUIRE_THAT(buf.bandPtr(0)[0],  WithinAbs(0.0f, 1e-4));      // (0,0)
    REQUIRE_THAT(buf.bandPtr(0)[16], WithinAbs(16.0f, 1e-4));     // (0,1) → 0 + 1*16
}

// TC-ACC-01 (FR-ACC-1) — per-band statistics match GDAL's within 1e-6. Doubles as the
// Phase-0 GdalOracle harness self-test, hence both tags.
TEST_CASE("TC-ACC-01 GdalOracle stats agree with RasterDataset within 1e-6", "[acc][harness][oracle]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(16, 16);

    auto oracle = GdalOracle::bandStats(fx.path, 1);
    REQUIRE(oracle.ok);

    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);
    auto st = ds->bandStats(1);

    REQUIRE_THAT(st.min,  WithinAbs(oracle.min,  1e-6));
    REQUIRE_THAT(st.max,  WithinAbs(oracle.max,  1e-6));
    REQUIRE_THAT(st.mean, WithinAbs(oracle.mean, 1e-6));
    // gradient 0..255: min=0, max=255
    REQUIRE_THAT(st.min, WithinAbs(0.0,   1e-6));
    REQUIRE_THAT(st.max, WithinAbs(255.0, 1e-6));
}

TEST_CASE("Harness: categorical fixture has exactly N distinct classes", "[harness][fixture]") {
    FixtureFactory ff;
    const int nclasses = 4;
    auto fx = ff.categorical(16, 16, nclasses);

    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);
    auto buf = ds->readRegion(0, 0, 16, 16, 16, 16, {1});
    REQUIRE(buf.isValid());

    std::set<int> classes;
    const float* p = buf.bandPtr(0);
    for (int i = 0; i < 16 * 16; ++i) classes.insert(static_cast<int>(std::lround(p[i])));
    REQUIRE(classes.size() == static_cast<size_t>(nclasses));
}

TEST_CASE("Harness: no-data fixture reports nodata and auto-stretch excludes it", "[harness][fixture]") {
    FixtureFactory ff;
    const double nd = -9999.0;
    auto fx = ff.withNoData(16, 16, nd);

    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);
    auto ndi = ds->noData(1);
    REQUIRE(ndi.has_value);
    REQUIRE_THAT(ndi.value, WithinAbs(nd, 1e-6));

    // The percentile stretch must ignore the no-data quadrant, so lo >> nd.
    auto [lo, hi] = ds->computeStretchPercentile(1, 2.0, 98.0);
    REQUIRE(lo > nd + 1.0f);
    REQUIRE(hi >= lo);
}

TEST_CASE("Harness: NDVI fixture matches the oracle pixel within 1e-6", "[harness][fixture]") {
    FixtureFactory ff;
    double expected = 0.0;
    auto fx = ff.ndviPair(0.1f, 0.5f, expected, 8, 8);  // (0.5-0.1)/(0.5+0.1)=0.6667

    double red = 0.0, nir = 0.0;
    REQUIRE(GdalOracle::readPixel(fx.path, 1, 3, 3, red));
    REQUIRE(GdalOracle::readPixel(fx.path, 2, 3, 3, nir));
    double ndvi = (nir - red) / (nir + red);
    REQUIRE_THAT(ndvi, WithinAbs(expected, 1e-6));
}

TEST_CASE("Harness: CRS-pair fixtures carry distinct CRS", "[harness][fixture]") {
    FixtureFactory ff;
    auto [a, b] = ff.crsPair(32, 32);

    auto dsA = RasterDataset::open(a.path);
    REQUIRE(dsA != nullptr);
    REQUIRE(dsA->crsWkt().find("4326") != std::string::npos);

    auto dsB = RasterDataset::open(b.path);
    if (dsB) {  // warped output requires PROJ data; skip gracefully if absent
        const std::string& wkt = dsB->crsWkt();
        REQUIRE((wkt.find("32633") != std::string::npos ||
                 wkt.find("UTM zone 33N") != std::string::npos));
    } else {
        SKIP("crsPair warp produced no output (PROJ data unavailable)");
    }
}

// ---- MockNam ----------------------------------------------------------------

TEST_CASE("Harness: MockNam returns a canned PNG that decodes", "[harness][net]") {
    MockNam nam;
    QByteArray png = makePng1x1(QColor(10, 20, 30));
    nam.setResponse("tile.example/", png, 200);

    QNetworkReply* reply = nam.get(QNetworkRequest(QUrl("https://tile.example/3/4/5.png")));
    waitForReply(reply);

    REQUIRE(reply->isFinished());
    REQUIRE(reply->error() == QNetworkReply::NoError);
    QImage img;
    REQUIRE(img.loadFromData(reply->readAll()));
    REQUIRE(img.width()  == 1);
    REQUIRE(img.height() == 1);
    REQUIRE(nam.requestCount() == 1);
    reply->deleteLater();
}

TEST_CASE("Harness: MockNam injects a TLS error", "[harness][net]") {
    MockNam nam;
    nam.setError("bad-cert.example/", QNetworkReply::SslHandshakeFailedError);

    QNetworkReply* reply = nam.get(QNetworkRequest(QUrl("https://bad-cert.example/x")));
    waitForReply(reply);

    REQUIRE(reply->isFinished());
    REQUIRE(reply->error() == QNetworkReply::SslHandshakeFailedError);
    reply->deleteLater();
}

// ---- GlTestHarness ----------------------------------------------------------

TEST_CASE("Harness: offscreen GL clears to a known colour", "[harness][gl]") {
    GlTestHarness gl(32, 32);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());

    REQUIRE(gl.makeCurrent());
    gl.clear(1.0f, 0.0f, 0.0f, 1.0f);   // red

    uint8_t px[4] = {0, 0, 0, 0};
    REQUIRE(gl.readPixel(16, 16, px));
    CHECK(px[0] == 255);
    CHECK(px[1] == 0);
    CHECK(px[2] == 0);
    CHECK(px[3] == 255);
}
