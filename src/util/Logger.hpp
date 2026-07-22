#pragma once
#include <memory>
#include <string>
#include <QString>
#include <spdlog/spdlog.h>

// Convenience macros — use these everywhere in the codebase
#define FV_TRACE(...)    ::Logger::instance().log()->trace(__VA_ARGS__)
#define FV_DEBUG(...)    ::Logger::instance().log()->debug(__VA_ARGS__)
#define FV_INFO(...)     ::Logger::instance().log()->info(__VA_ARGS__)
#define FV_WARN(...)     ::Logger::instance().log()->warn(__VA_ARGS__)
#define FV_ERROR(...)    ::Logger::instance().log()->error(__VA_ARGS__)
#define FV_CRITICAL(...) ::Logger::instance().log()->critical(__VA_ARGS__)

class QObject;
class LogRelay;

class Logger {
public:
    static Logger& instance();

    spdlog::logger* log() { return m_logger.get(); }

    // Called once by Application before any window is shown
    void init(QObject* parent = nullptr);

    // The relay QObject emits messageLogged(int level, QString text).
    // Connect to this from LogWidget.
    QObject* relay() const;

    // Absolute path to the on-disk rotating application log (FR-ERR-8), or empty
    // if the file sink could not be created.
    QString logFilePath() const;

private:
    Logger();
    std::shared_ptr<spdlog::logger> m_logger;
    LogRelay*                        m_relay{nullptr};
    QString                          m_log_file;
};
