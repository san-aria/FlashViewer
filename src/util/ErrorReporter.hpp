#pragma once
#include <QObject>
#include <QString>
#include <exception>

// Central error pathway (SDD §12.4, FR-ERR-1…8, FR-SEC-4).
//
// A single funnel for GDAL errors (via an installed CPLSetErrorHandler) and caught
// C++ exceptions. Every report is (a) logged through the Logger/spdlog pipeline —
// so it lands in the in-app log panel and the on-disk log file — and (b) emitted as
// reported() so the UI can raise a non-fatal status banner. Levels are spdlog
// numeric levels: 1=debug, 2=info, 3=warn, 4=err, 5=critical.
class ErrorReporter : public QObject {
    Q_OBJECT
public:
    static ErrorReporter& instance();

    // Install the process-wide GDAL error handler. Call once at startup.
    static void install();

    // Log at `level` and emit reported(). Safe to call from any thread (the
    // reported() signal is delivered via a queued connection to the UI).
    void report(int level, const QString& context, const QString& message);

    // Run `fn`, catching std::exception (and any other throw) so it can never
    // escape the Qt event loop (slot-level boundary, NFR-REL-2). Reports the
    // failure non-fatally. Returns true when `fn` completed without throwing.
    template <class F>
    static bool runGuarded(const char* context, F&& fn) {
        try {
            fn();
            return true;
        } catch (const std::exception& e) {
            instance().report(4, QString::fromUtf8(context),
                              QString::fromUtf8(e.what()));
        } catch (...) {
            instance().report(4, QString::fromUtf8(context),
                              QStringLiteral("unknown exception"));
        }
        return false;
    }

signals:
    void reported(int level, const QString& message);

private:
    ErrorReporter() = default;
};

// Wrap a risky UI-action slot body: FV_GUARD("Open") { ...may throw... };
#define FV_GUARD(ctx) ::ErrorReporter::runGuarded((ctx), [&]()
