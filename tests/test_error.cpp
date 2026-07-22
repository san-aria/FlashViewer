#include <catch2/catch_test_macros.hpp>
#include "util/ErrorReporter.hpp"
#include "io/RasterDataset.hpp"

#include <QObject>
#include <QString>
#include <stdexcept>

// FR-ERR-8: every reported error is logged AND surfaced via reported() so the UI
// can raise its non-fatal banner. Direct (same-thread) connection fires
// synchronously, so a captured flag suffices — no event loop needed.
TEST_CASE("ErrorReporter: report emits reported() with level + message", "[TC-ERR-08]") {
    int gotLevel = -1;
    QString gotMsg;
    QObject ctx;   // scoping the connection to ctx auto-disconnects at test end
    QObject::connect(&ErrorReporter::instance(), &ErrorReporter::reported, &ctx,
        [&](int lvl, const QString& m) { gotLevel = lvl; gotMsg = m; });

    ErrorReporter::instance().report(4, "UnitTest", "boom");

    REQUIRE(gotLevel == 4);
    REQUIRE(gotMsg.contains("boom"));
    REQUIRE(gotMsg.contains("UnitTest"));
}

// NFR-REL-2 / slot-level boundary: a throwing task is contained, reported at error
// level, and reported() still fires — the exception never escapes.
TEST_CASE("ErrorReporter::runGuarded contains exceptions", "[TC-ERR-01]") {
    int gotLevel = -1;
    QString gotMsg;
    QObject ctx;
    QObject::connect(&ErrorReporter::instance(), &ErrorReporter::reported, &ctx,
        [&](int lvl, const QString& m) { gotLevel = lvl; gotMsg = m; });

    const bool ok = ErrorReporter::runGuarded("Task", [] {
        throw std::runtime_error("nope");
    });
    REQUIRE_FALSE(ok);
    REQUIRE(gotLevel == 4);
    REQUIRE(gotMsg.contains("nope"));

    const bool ok2 = ErrorReporter::runGuarded("Task2", [] { /* no throw */ });
    REQUIRE(ok2);
}

// FR-ERR-1 / FR-ERR-6: an unopenable input is contained — null result, no crash.
TEST_CASE("RasterDataset::open contains an unopenable input", "[TC-ERR-01]") {
    auto ds = RasterDataset::open("this_path_does_not_exist_12345.tif");
    REQUIRE(ds == nullptr);
}
