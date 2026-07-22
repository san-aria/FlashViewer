#include "util/Logger.hpp"

#include <QObject>
#include <QString>
#include <QStandardPaths>
#include <QDir>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/log_msg.h>
#include <chrono>
#include <mutex>

// ---- Qt relay object --------------------------------------------------------

class LogRelay : public QObject {
    Q_OBJECT
public:
    explicit LogRelay(QObject* parent = nullptr) : QObject(parent) {}
signals:
    void messageLogged(int level, const QString& text);
};

// ---- Custom spdlog sink that posts to LogRelay on the Qt main thread --------

class QtSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit QtSink(LogRelay* relay) : m_relay(relay) {}
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        QString text = QString::fromUtf8(buf.data(), static_cast<int>(buf.size())).trimmed();
        int lvl = static_cast<int>(msg.level);
        // Use QueuedConnection so the call lands on the main thread
        QMetaObject::invokeMethod(m_relay, "messageLogged",
            Qt::QueuedConnection,
            Q_ARG(int, lvl),
            Q_ARG(QString, text));
    }
    void flush_() override {}
private:
    LogRelay* m_relay;
};

// ---- Logger -----------------------------------------------------------------

Logger::Logger() = default;

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(QObject* parent) {
    m_relay = new LogRelay(parent);

    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto qt_sink     = std::make_shared<QtSink>(m_relay);

    std::vector<spdlog::sink_ptr> sinks{stdout_sink, qt_sink};

    // Persistent on-disk application log (FR-ERR-8): a rotating file sink under the
    // per-user app-data dir. Requires org/app name to be set (Application ctor does
    // so before init()). Best-effort — a sink-open failure must not break logging.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dir.isEmpty()) {
        QDir().mkpath(dir + "/logs");
        m_log_file = dir + "/logs/flashviewer.log";
        try {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                m_log_file.toStdString(), 2ull * 1024 * 1024, 3);  // 2 MB × 3 files
            sinks.push_back(file_sink);
        } catch (const std::exception&) {
            m_log_file.clear();  // fall back to stdout + Qt sinks only
        }
    }

    m_logger = std::make_shared<spdlog::logger>(
        "flashviewer", sinks.begin(), sinks.end());

    m_logger->set_level(spdlog::level::debug);
    m_logger->flush_on(spdlog::level::warn);
    m_logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(m_logger);
    // Periodic flush so buffered info/debug lines reach the log file even if the
    // process is terminated abnormally (warn/error already flush immediately).
    spdlog::flush_every(std::chrono::seconds(3));
    // NB: do NOT emit a log line here — Catch2's test-discovery runs the binary and
    // parses stdout as JSON, which any stdout log during init() would corrupt. The
    // log-file path is available via logFilePath(); the app logs it after startup.
}

QObject* Logger::relay() const { return m_relay; }

QString Logger::logFilePath() const { return m_log_file; }

#include "Logger.moc"
