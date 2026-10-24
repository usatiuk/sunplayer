#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <functional>
#include <optional>

#include "media/MediaInput.h"

struct JellyfinItem {
    QString id;
    QString name;
    QString overview;
    QString type;
    QString imageTag;
    QString backdropItemId;
    QString backdropTag;
    QString seriesName;
    int year = 0;
    int seasonNumber = -1;
    int episodeNumber = -1;
    bool folder = false;
    bool watched = false;
    qlonglong positionMilliseconds = 0;
    qlonglong durationMilliseconds = 0;
};

struct JellyfinPlayback {
    MediaInput input;
    QString itemId;
    QString sourceId;
    QString playSessionId;
    qlonglong startingPositionMilliseconds = 0;
};

// One authenticated server session. All service HTTP construction and JSON
// validation stays here; consumers receive only the small values they use.
class JellyfinClient final : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(QJsonObject, QString)>;
    explicit JellyfinClient(QObject* parent = nullptr);
    bool authenticated() const { return !m_token.isEmpty(); }
    QUrl server() const { return m_server; }
    QString userId() const { return m_userId; }
    QString token() const { return m_token; }
    bool setSession(QUrl server, QString userId, QString token, QString deviceId);
    void clearSession();
    void login(QUrl const& server, QString const& user, QString const& password, QString const& deviceId,
               std::function<void(QString)> done);
    QNetworkReply* request(QByteArray method, QString const& path, QUrlQuery const& query, QJsonObject const& body,
                           Completion done, QObject* context = nullptr);
    void libraries(std::function<void(QList<JellyfinItem>, QString)> done);
    void items(QString const& parent, int start, std::function<void(QList<JellyfinItem>, int, QString)> done);
    void resume(std::function<void(QList<JellyfinItem>, QString)> done);
    void recent(bool shows, std::function<void(QList<JellyfinItem>, QString)> done);
    void ancestors(QString const& id, std::function<void(QList<JellyfinItem>, QString)> done);
    void item(QString const& id, std::function<void(JellyfinItem, QString)> done);
    void resolve(JellyfinItem const& item, bool fromStart, std::function<void(JellyfinPlayback, QString)> done,
                 QObject* context);
    void setWatched(QString const& id, bool watched, std::function<void(QString)> done);
    void report(JellyfinPlayback const& playback, bool stopped, qlonglong positionMilliseconds, bool seekable,
                std::function<void(QString)> done, QObject* context);
    QNetworkReply* image(QString const& id, QString const& tag, QString const& type = QStringLiteral("Primary"));
    void abortRequests();

  signals:
    void authenticationLost();

  private:
    QNetworkReply* requestDocument(QByteArray method, QString const& path, QUrlQuery const& query,
                                   QJsonObject const& body, std::function<void(QJsonDocument, QString)> done,
                                   QObject* context = nullptr);
    void queryItems(QString const& path, QUrlQuery query, std::function<void(QList<JellyfinItem>, QString)> done);
    QUrl endpoint(QString const& path, QUrlQuery const& query = {}) const;
    QNetworkRequest authorizedRequest(QUrl const& url) const;
    QNetworkAccessManager m_network;
    QUrl m_server;
    QString m_userId;
    QString m_token;
    QString m_deviceId;
};
