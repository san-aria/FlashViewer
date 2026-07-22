#pragma once
// MockNam — a QNetworkAccessManager that answers requests from canned rules
// instead of hitting the network, so OSM-basemap and remote-fetch tests run
// offline and deterministically (docs/TEST_SPEC.md §2: "MockNam").
//
// Rules are matched by substring against the request URL, in insertion order;
// the first match wins. A default response may be set for unmatched URLs.
// A rule can return either a body (with an HTTP status) or a network error
// (e.g. SslHandshakeFailedError for the TLS-verification test).

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QString>
#include <vector>

class MockNam : public QNetworkAccessManager {
public:
    explicit MockNam(QObject* parent = nullptr) : QNetworkAccessManager(parent) {}

    void setResponse(const QString& urlContains, const QByteArray& body, int httpStatus = 200);
    void setError(const QString& urlContains, QNetworkReply::NetworkError err);
    void setDefaultResponse(const QByteArray& body, int httpStatus = 200);

    int requestCount() const { return m_request_count; }

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& req,
                                 QIODevice* outgoingData) override;

private:
    struct Rule {
        QString                       match;
        QByteArray                    body;
        QNetworkReply::NetworkError   err{QNetworkReply::NoError};
        int                           status{200};
        bool                          is_error{false};
    };
    std::vector<Rule> m_rules;
    Rule              m_default;
    bool              m_has_default{false};
    int               m_request_count{0};
};
