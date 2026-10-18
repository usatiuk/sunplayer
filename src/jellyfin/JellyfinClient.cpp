#include "jellyfin/JellyfinClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>
#include <limits>

namespace {
void boundReply(QNetworkReply* reply) {
    QTimer::singleShot(10000, reply, [reply] {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > 8 * 1024 * 1024) {
            reply->abort();
        }
    });
}
bool validServer(QUrl const& server) {
    return server.isValid() && !server.host().isEmpty() && server.userInfo().isEmpty() && !server.hasQuery() &&
           !server.hasFragment() &&
           (server.scheme() == QStringLiteral("http") || server.scheme() == QStringLiteral("https"));
}
bool validId(QString const& value) {
    if (value.isEmpty() || value.size() > 64) {
        return false;
    }
    for (QChar c : value) {
        if (!c.isLetterOrNumber() && c != u'-') {
            return false;
        }
    }
    return true;
}
qlonglong ticksToMilliseconds(QJsonValue value) {
    double ticks = value.toDouble(-1);
    if (!std::isfinite(ticks) || ticks < 0 || ticks / 10000 > double(std::numeric_limits<qlonglong>::max())) {
        return 0;
    }
    return qlonglong(ticks / 10000);
}
std::optional<JellyfinItem> parseItem(QJsonObject const& object) {
    JellyfinItem item;
    item.id = object.value(QStringLiteral("Id")).toString();
    item.name = object.value(QStringLiteral("Name")).toString();
    item.type = object.value(QStringLiteral("Type")).toString();
    if (!validId(item.id) || item.name.isEmpty()) {
        return {};
    }
    item.overview = object.value(QStringLiteral("Overview")).toString();
    item.folder = object.value(QStringLiteral("IsFolder")).toBool();
    item.imageTag = object.value(QStringLiteral("ImageTags")).toObject().value(QStringLiteral("Primary")).toString();
    auto const backdrops = object.value(QStringLiteral("BackdropImageTags")).toArray();
    if (!backdrops.isEmpty()) {
        item.backdropTag = backdrops.first().toString();
    }
    item.seriesName = object.value(QStringLiteral("SeriesName")).toString();
    item.year = object.value(QStringLiteral("ProductionYear")).toInt();
    item.seasonNumber = object.value(QStringLiteral("ParentIndexNumber")).toInt(-1);
    item.episodeNumber = object.value(QStringLiteral("IndexNumber")).toInt(-1);
    auto user = object.value(QStringLiteral("UserData")).toObject();
    item.watched = user.value(QStringLiteral("Played")).toBool();
    item.positionMilliseconds = ticksToMilliseconds(user.value(QStringLiteral("PlaybackPositionTicks")));
    item.durationMilliseconds = ticksToMilliseconds(object.value(QStringLiteral("RunTimeTicks")));
    return item;
}
QList<JellyfinItem> parseItems(QJsonObject const& object) {
    QList<JellyfinItem> items;
    for (auto value : object.value(QStringLiteral("Items")).toArray()) {
        if (auto item = parseItem(value.toObject())) {
            items.push_back(std::move(*item));
        }
    }
    return items;
}
} // namespace

JellyfinClient::JellyfinClient(QObject* parent) : QObject(parent) {}

bool JellyfinClient::setSession(QUrl server, QString userId, QString token, QString deviceId) {
    if (!validServer(server) || !validId(userId) || token.isEmpty() || token.contains(u'"') || token.contains(u'\r') ||
        token.contains(u'\n') || !validId(deviceId)) {
        return false;
    }
    m_server = std::move(server);
    m_userId = std::move(userId);
    m_token = std::move(token);
    m_deviceId = std::move(deviceId);
    return true;
}
void JellyfinClient::clearSession() {
    abortRequests();
    m_token.clear();
    m_userId.clear();
}
void JellyfinClient::abortRequests() {
    auto replies = m_network.findChildren<QNetworkReply*>();
    for (auto* reply : replies) {
        reply->abort();
    }
}
QUrl JellyfinClient::endpoint(QString const& path, QUrlQuery const& query) const {
    QUrl url = m_server;
    QString base = url.path();
    if (!base.endsWith(u'/')) {
        base += u'/';
    }
    url.setPath(base + path);
    url.setQuery(query);
    return url;
}
QNetworkRequest JellyfinClient::authorizedRequest(QUrl const& url) const {
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    // API and artwork credentials never cross an HTTP redirect, even same-origin.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QByteArray auth = "MediaBrowser Client=\"SunPlayer\", Device=\"Desktop\", DeviceId=\"" + m_deviceId.toUtf8() +
                      "\", Version=\"" + QCoreApplication::applicationVersion().toUtf8() + "\"";
    if (!m_token.isEmpty()) {
        auth += ", Token=\"" + m_token.toUtf8() + "\"";
    }
    request.setRawHeader("Authorization", auth);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}
QNetworkReply* JellyfinClient::request(QByteArray method, QString const& path, QUrlQuery const& query,
                                       QJsonObject const& body, Completion done, QObject* context) {
    auto* reply =
        m_network.sendCustomRequest(authorizedRequest(endpoint(path, query)), method,
                                    body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    boundReply(reply);
    connect(reply, &QNetworkReply::finished, context ? context : this, [this, reply, done = std::move(done)] {
        int const status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            done({}, tr("The server request was cancelled or timed out."));
            return;
        }
        if (status == 401 && !m_token.isEmpty()) {
            m_token.clear();
            emit authenticationLost();
        }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            done({},
                 status == 401
                     ? tr("Login was rejected or has expired. Sign in again.")
                     : tr("The server request failed (HTTP %1). Check the connection and server address.").arg(status));
            return;
        }
        QByteArray const bytes = reply->readAll();
        if (bytes.isEmpty()) {
            done({}, {});
            return;
        }
        QJsonParseError error;
        auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            done({}, tr("The server returned an invalid response."));
            return;
        }
        done(document.object(), {});
    });
    return reply;
}
void JellyfinClient::login(QUrl const& server, QString const& user, QString const& password, QString const& deviceId,
                           std::function<void(QString)> done) {
    if (!validServer(server)) {
        done(tr("Enter a complete http:// or https:// server address."));
        return;
    }
    abortRequests();
    m_server = server;
    m_userId.clear();
    m_token.clear();
    m_deviceId = deviceId;
    request("POST", QStringLiteral("Users/AuthenticateByName"), {},
            {{QStringLiteral("Username"), user}, {QStringLiteral("Pw"), password}},
            [this, done = std::move(done)](QJsonObject object, QString error) {
                if (!error.isEmpty()) {
                    done(error);
                    return;
                }
                QString token = object.value(QStringLiteral("AccessToken")).toString();
                QString userId = object.value(QStringLiteral("User")).toObject().value(QStringLiteral("Id")).toString();
                if (token.isEmpty() || token.contains(u'\r') || token.contains(u'\n') || token.contains(u'"') ||
                    !validId(userId)) {
                    done(tr("The server returned an invalid login session."));
                    return;
                }
                m_token = std::move(token);
                m_userId = std::move(userId);
                done({});
            });
}
void JellyfinClient::libraries(std::function<void(QList<JellyfinItem>, QString)> done) {
    request("GET", QStringLiteral("Users/%1/Views").arg(m_userId), {}, {},
            [done = std::move(done)](QJsonObject object, QString error) {
                if (error.isEmpty() && !object.value(QStringLiteral("Items")).isArray()) {
                    error = tr("The server returned an invalid library list.");
                }
                done(parseItems(object), error);
            });
}
void JellyfinClient::items(QString const& parent, int start,
                           std::function<void(QList<JellyfinItem>, int, QString)> done) {
    Q_ASSERT(validId(parent));
    QUrlQuery query{{QStringLiteral("UserId"), m_userId},
                    {QStringLiteral("ParentId"), parent},
                    {QStringLiteral("StartIndex"), QString::number(start)},
                    {QStringLiteral("Limit"), QStringLiteral("40")},
                    {QStringLiteral("Fields"), QStringLiteral("Overview")},
                    {QStringLiteral("EnableUserData"), QStringLiteral("true")},
                    {QStringLiteral("SortBy"), QStringLiteral("ParentIndexNumber,IndexNumber,SortName")}};
    request("GET", QStringLiteral("Items"), query, {}, [done = std::move(done)](QJsonObject object, QString error) {
        if (error.isEmpty() && (!object.value(QStringLiteral("Items")).isArray() ||
                                !object.value(QStringLiteral("TotalRecordCount")).isDouble())) {
            error = tr("The server returned an invalid item page.");
        }
        done(parseItems(object), object.value(QStringLiteral("TotalRecordCount")).toInt(), error);
    });
}
void JellyfinClient::queryItems(QString const& path, QUrlQuery query,
                                std::function<void(QList<JellyfinItem>, QString)> done) {
    query.addQueryItem(QStringLiteral("UserId"), m_userId);
    query.addQueryItem(QStringLiteral("Limit"), QStringLiteral("12"));
    query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
    request("GET", path, query, {}, [done = std::move(done)](QJsonObject object, QString error) {
        if (error.isEmpty() && !object.value(QStringLiteral("Items")).isArray()) {
            error = tr("The server returned an invalid item list.");
        }
        done(parseItems(object), error);
    });
}
void JellyfinClient::resume(std::function<void(QList<JellyfinItem>, QString)> done) {
    queryItems(QStringLiteral("UserItems/Resume"),
               {{QStringLiteral("MediaTypes"), QStringLiteral("Video")},
                {QStringLiteral("IncludeItemTypes"), QStringLiteral("Movie,Episode")}},
               std::move(done));
}
void JellyfinClient::recent(bool shows, std::function<void(QList<JellyfinItem>, QString)> done) {
    queryItems(QStringLiteral("Items"),
               {{QStringLiteral("IncludeItemTypes"), shows ? QStringLiteral("Series") : QStringLiteral("Movie")},
                {QStringLiteral("Recursive"), QStringLiteral("true")},
                {QStringLiteral("SortBy"), QStringLiteral("DateCreated")},
                {QStringLiteral("SortOrder"), QStringLiteral("Descending")}},
               std::move(done));
}
void JellyfinClient::item(QString const& id, std::function<void(JellyfinItem, QString)> done) {
    Q_ASSERT(validId(id));
    request("GET", QStringLiteral("Users/%1/Items/%2").arg(m_userId, id), {}, {},
            [done = std::move(done)](QJsonObject object, QString error) {
                auto item = parseItem(object);
                if (error.isEmpty() && !item) {
                    error = tr("The server returned an invalid item.");
                }
                done(item.value_or(JellyfinItem{}), error);
            });
}
void JellyfinClient::resolve(JellyfinItem const& item, bool fromStart,
                             std::function<void(JellyfinPlayback, QString)> done, QObject* context) {
    Q_ASSERT(validId(item.id));
    // V1 opens the original file with native FFmpeg. The basic endpoint returns
    // sources/session identity without applying device-profile bitrate or
    // subtitle-conversion constraints meant for negotiated playback.
    request(
        "GET", QStringLiteral("Items/%1/PlaybackInfo").arg(item.id), {{QStringLiteral("UserId"), m_userId}}, {},
        [this, item, fromStart, done = std::move(done)](QJsonObject object, QString error) {
            if (!error.isEmpty()) {
                done({}, error);
                return;
            }
            if (!validId(object.value(QStringLiteral("PlaySessionId")).toString())) {
                done({}, tr("The server did not return a valid playback session."));
                return;
            }
            for (auto value : object.value(QStringLiteral("MediaSources")).toArray()) {
                auto source = value.toObject();
                QString id = source.value(QStringLiteral("Id")).toString();
                if (!validId(id) || !source.value(QStringLiteral("SupportsDirectPlay")).toBool() ||
                    source.value(QStringLiteral("RequiresOpening")).toBool()) {
                    continue;
                }
                JellyfinPlayback playback;
                playback.itemId = item.id;
                playback.sourceId = id;
                playback.playSessionId = object.value(QStringLiteral("PlaySessionId")).toString();
                playback.startingPositionMilliseconds = fromStart || item.watched ? 0 : item.positionMilliseconds;
                QUrlQuery query{{QStringLiteral("Static"), QStringLiteral("true")},
                                {QStringLiteral("MediaSourceId"), id},
                                {QStringLiteral("PlaySessionId"), playback.playSessionId},
                                {QStringLiteral("api_key"), m_token}};
                playback.input =
                    MediaInput(endpoint(QStringLiteral("Videos/%1/stream").arg(item.id), query), item.name);
                done(std::move(playback), {});
                return;
            }
            done({}, tr("This item cannot be played as an original file. Transcoding is not supported."));
        },
        context);
}
void JellyfinClient::setWatched(QString const& id, bool watched, std::function<void(QString)> done) {
    Q_ASSERT(validId(id));
    request(watched ? "POST" : "DELETE", QStringLiteral("Users/%1/PlayedItems/%2").arg(m_userId, id), {}, {},
            [done = std::move(done)](QJsonObject, QString error) { done(error); });
}
void JellyfinClient::report(JellyfinPlayback const& playback, bool stopped, qlonglong position, bool seekable,
                            std::function<void(QString)> done, QObject* context) {
    Q_ASSERT(position >= 0 && position <= std::numeric_limits<qlonglong>::max() / 10000);
    QJsonObject body{{QStringLiteral("ItemId"), playback.itemId},
                     {QStringLiteral("MediaSourceId"), playback.sourceId},
                     {QStringLiteral("PlaySessionId"), playback.playSessionId},
                     {QStringLiteral("PositionTicks"), double(position) * 10000},
                     {QStringLiteral("PlayMethod"), QStringLiteral("DirectPlay")},
                     {QStringLiteral("CanSeek"), seekable}};
    request(
        "POST", stopped ? QStringLiteral("Sessions/Playing/Stopped") : QStringLiteral("Sessions/Playing"), {}, body,
        [done = std::move(done)](QJsonObject, QString error) { done(error); }, context);
}
QNetworkReply* JellyfinClient::image(QString const& id, QString const& tag, QString const& type) {
    if (!validId(id) || (type != QStringLiteral("Primary") && type != QStringLiteral("Backdrop"))) {
        return nullptr;
    }
    auto* reply = m_network.get(authorizedRequest(
        endpoint(QStringLiteral("Items/%1/Images/%2").arg(id, type),
                 {{QStringLiteral("MaxWidth"),
                   type == QStringLiteral("Backdrop") ? QStringLiteral("1280") : QStringLiteral("480")},
                  {QStringLiteral("Tag"), tag}})));
    boundReply(reply);
    return reply;
}
