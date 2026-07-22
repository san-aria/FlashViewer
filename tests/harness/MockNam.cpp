#include "MockNam.hpp"

#include <QTimer>
#include <QVariant>
#include <algorithm>
#include <cstring>
#include <utility>

namespace {

// Minimal in-memory QNetworkReply that serves a fixed byte buffer (or an error)
// and finishes asynchronously on the next event-loop turn.
class FakeReply : public QNetworkReply {
public:
    FakeReply(QNetworkAccessManager::Operation op,
              const QNetworkRequest& req,
              QByteArray body,
              QNetworkReply::NetworkError err,
              int httpStatus,
              QObject* parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(req);
        setOperation(op);
        setUrl(req.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);

        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
        setHeader(QNetworkRequest::ContentLengthHeader,
                  static_cast<qint64>(m_body.size()));

        if (err != QNetworkReply::NoError)
            setError(err, QStringLiteral("MockNam injected error"));

        QTimer::singleShot(0, this, [this, err] {
            if (err != QNetworkReply::NoError)
                emit errorOccurred(err);
            emit metaDataChanged();
            if (!m_body.isEmpty()) emit readyRead();
            setFinished(true);
            emit finished();
        });
    }

    void abort() override {
        setError(OperationCanceledError, QStringLiteral("aborted"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override {
        return (m_body.size() - m_off) + QNetworkReply::bytesAvailable();
    }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const qint64 remaining = m_body.size() - m_off;
        if (remaining <= 0) return -1;            // EOF
        const qint64 n = std::min(maxlen, remaining);
        std::memcpy(data, m_body.constData() + m_off, static_cast<size_t>(n));
        m_off += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64     m_off{0};
};

} // namespace

void MockNam::setResponse(const QString& urlContains, const QByteArray& body, int httpStatus) {
    m_rules.push_back({urlContains, body, QNetworkReply::NoError, httpStatus, false});
}

void MockNam::setError(const QString& urlContains, QNetworkReply::NetworkError err) {
    m_rules.push_back({urlContains, {}, err, 0, true});
}

void MockNam::setDefaultResponse(const QByteArray& body, int httpStatus) {
    m_default = {{}, body, QNetworkReply::NoError, httpStatus, false};
    m_has_default = true;
}

QNetworkReply* MockNam::createRequest(Operation op, const QNetworkRequest& req,
                                      QIODevice* /*outgoingData*/) {
    ++m_request_count;
    const QString url = req.url().toString();

    for (const auto& rule : m_rules) {
        if (url.contains(rule.match)) {
            return new FakeReply(op, req, rule.body, rule.err, rule.status, this);
        }
    }
    if (m_has_default)
        return new FakeReply(op, req, m_default.body, QNetworkReply::NoError,
                             m_default.status, this);

    // Unmatched and no default → host-not-found, so tests notice missing rules.
    return new FakeReply(op, req, {}, QNetworkReply::HostNotFoundError, 0, this);
}
