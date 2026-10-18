#pragma once
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <memory>
#include <utility>

// Controlled HTTP edge, not an implementation of Jellyfin policy. Actual
// container parsing, decode, seek and player lifetime use production code.
class HttpEdge final : public QTcpServer {
  public:
    struct Request {
        QByteArray method;
        QUrl url;
        QJsonObject body;
        QByteArray headers;
        int contentLength = 0;
    };
    QList<Request> requests;
    QByteArray media;
    bool galleryFixtures = false;
    bool failResume = false;
    bool delayFeeds = false;
    bool groupedViews = false;
    bool delayAncestors = false;
    QList<std::pair<QPointer<QTcpSocket>, QByteArray>> delayedAncestors;
    QList<std::pair<QPointer<QTcpSocket>, QJsonObject>> delayedFeeds;
    bool stall = false;
    bool delayStop = false;
    bool delayWatched = false;
    QList<QPointer<QTcpSocket>> delayedWatched;
    bool rejectLibraries = false;
    bool rejectMedia = false;
    bool pauseMedia = false;
    QUrl redirect;
    QList<std::pair<QPointer<QTcpSocket>, QByteArray>> pausedMedia;
    QList<QPointer<QTcpSocket>> delayedStops;
    HttpEdge() {
        QFile file(QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264-seek.mkv"));
        if (!file.open(QIODevice::ReadOnly)) {
            qFatal("Missing HTTP media fixture");
        }
        media = file.readAll();
        if (!listen(QHostAddress::LocalHost)) {
            qFatal("Could not listen for test HTTP requests");
        }
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    int end = buffer->indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("handled").toBool()) {
                        return;
                    }
                    auto lines = buffer->left(end).split('\n');
                    int length = 0;
                    QByteArray range;
                    for (auto line : lines) {
                        if (line.toLower().startsWith("content-length:")) {
                            length = line.mid(15).trimmed().toInt();
                        }
                        if (line.toLower().startsWith("range:")) {
                            range = line.mid(6).trimmed();
                        }
                    }
                    if (buffer->size() < end + 4 + length) {
                        return;
                    }
                    socket->setProperty("handled", true);
                    auto first = lines.front().trimmed().split(' ');
                    Request request{first[0], QUrl::fromEncoded(first[1]),
                                    QJsonDocument::fromJson(buffer->mid(end + 4, length)).object(), buffer->left(end),
                                    length};
                    requests.push_back(request);
                    QString path = request.url.path();
                    if (stall) {
                        return;
                    }
                    if (rejectLibraries && path.endsWith(QStringLiteral("/Views"))) {
                        respond(socket, {}, 401);
                        return;
                    }
                    if (path.endsWith(QStringLiteral("/stream"))) {
                        if (rejectMedia) {
                            respond(socket, {}, 403);
                            return;
                        }
                        if (!redirect.isEmpty()) {
                            respond(socket, {}, 302, "Location: " + redirect.toEncoded() + "\r\n");
                            return;
                        }
                        qlonglong start = 0;
                        if (range.startsWith("bytes=")) {
                            start = range.mid(6).split('-').front().toLongLong();
                        }
                        if (start < 0 || start >= media.size()) {
                            respond(socket, {}, 416);
                            return;
                        }
                        QByteArray headers = "Accept-Ranges: bytes\r\nContent-Type: video/x-matroska\r\n";
                        if (!range.isEmpty()) {
                            headers += "Content-Range: bytes " + QByteArray::number(start) + "-" +
                                       QByteArray::number(media.size() - 1) + "/" + QByteArray::number(media.size()) +
                                       "\r\n";
                        }
                        auto body = media.mid(start);
                        if (pauseMedia && body.size() > 8192) {
                            socket->write(
                                "HTTP/1.1 " + QByteArray::number(range.isEmpty() ? 200 : 206) +
                                " OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) +
                                "\r\n" + headers + "\r\n" + body.left(body.size() - 2048));
                            pausedMedia.push_back({socket, body.right(2048)});
                        } else {
                            respond(socket, body, range.isEmpty() ? 200 : 206, headers);
                        }
                        return;
                    }
                    if (path.endsWith(QStringLiteral("AuthenticateByName"))) {
                        respondJson(socket, {{QStringLiteral("AccessToken"), QStringLiteral("test-token")},
                                             {QStringLiteral("User"),
                                              QJsonObject{{QStringLiteral("Id"), QStringLiteral("abc123")}}}});
                        return;
                    }
                    if (path.endsWith(QStringLiteral("PlaybackInfo"))) {
                        if (request.method != "GET") {
                            respond(socket, {}, 405);
                            return;
                        }
                        respondJson(
                            socket,
                            {{QStringLiteral("PlaySessionId"), path.section(u'/', -2, -2) + QStringLiteral("-session")},
                             {QStringLiteral("MediaSources"),
                              QJsonArray{QJsonObject{
                                  {QStringLiteral("Id"), path.section(u'/', -2, -2) + QStringLiteral("-source")},
                                  {QStringLiteral("SupportsDirectPlay"), true}}}}});
                        return;
                    }
                    if (path.endsWith(QStringLiteral("/Stopped")) && delayStop) {
                        delayedStops.push_back(socket);
                        return;
                    }
                    if (path.contains(QStringLiteral("PlayedItems")) && delayWatched) {
                        delayedWatched.push_back(socket);
                        return;
                    }
                    if (path.endsWith(QStringLiteral("force401"))) {
                        respond(socket, {}, 401);
                        return;
                    }
                    if (galleryFixtures && path.contains(QStringLiteral("/Images/"))) {
                        QImage image(path.contains(QStringLiteral("Backdrop")) ? QSize(640, 360) : QSize(240, 360),
                                     QImage::Format_RGB32);
                        uint seed = qHash(path);
                        for (int y = 0; y < image.height(); ++y) {
                            for (int x = 0; x < image.width(); ++x) {
                                image.setPixelColor(
                                    x, y, QColor::fromHsv((seed + x / 3 + y / 4) % 360, 155, 90 + (x + y) % 130));
                            }
                        }
                        QByteArray bytes;
                        QBuffer buffer(&bytes);
                        buffer.open(QIODevice::WriteOnly);
                        image.save(&buffer, "PNG");
                        respond(socket, bytes, 200, "Content-Type: image/png\r\n");
                        return;
                    }
                    if (galleryFixtures &&
                        (path.endsWith(QStringLiteral("/Views")) || path.endsWith(QStringLiteral("/Items")) ||
                         path.endsWith(QStringLiteral("/Resume")) || path.endsWith(QStringLiteral("/Ancestors")) ||
                         path.contains(QStringLiteral("/Users/abc123/Items/")))) {
                        auto makeItem = [](QString id, QString name, QString type, bool folder) {
                            return QJsonObject{
                                {QStringLiteral("Id"), id},
                                {QStringLiteral("Name"), name},
                                {QStringLiteral("Type"), type},
                                {QStringLiteral("IsFolder"), folder},
                                {QStringLiteral("Overview"),
                                 QStringLiteral(
                                     "A quiet discovery becomes an extraordinary journey. A full description remains "
                                     "readable here, with artwork and actions kept together.")},
                                {QStringLiteral("ProductionYear"), 2024},
                                {QStringLiteral("RunTimeTicks"), 100000000},
                                {QStringLiteral("ImageTags"),
                                 QJsonObject{{QStringLiteral("Primary"), QStringLiteral("poster")}}},
                                {QStringLiteral("BackdropImageTags"), QJsonArray{QStringLiteral("backdrop")}},
                                {QStringLiteral("UserData"),
                                 QJsonObject{{QStringLiteral("Played"), false},
                                             {QStringLiteral("PlaybackPositionTicks"), 25000000}}}};
                        };
                        auto movie = makeItem(QStringLiteral("movieHome"), QStringLiteral("The Last Horizon"),
                                              QStringLiteral("Movie"), false);
                        auto show = makeItem(QStringLiteral("showHome"), QStringLiteral("Beyond the Ordinary"),
                                             QStringLiteral("Series"), true);
                        auto season = makeItem(QStringLiteral("seasonHome"), QStringLiteral("Season 1"),
                                               QStringLiteral("Season"), true);
                        auto episode = makeItem(QStringLiteral("episodeHome"), QStringLiteral("1. A New Beginning"),
                                                QStringLiteral("Episode"), false);
                        episode[QStringLiteral("SeriesName")] = QStringLiteral("Beyond the Ordinary");
                        episode[QStringLiteral("ParentIndexNumber")] = 1;
                        episode[QStringLiteral("IndexNumber")] = 1;
                        if (path.endsWith(QStringLiteral("/Ancestors"))) {
                            QJsonArray parents;
                            auto const id = path.section(u'/', -2, -2);
                            if (id == QStringLiteral("episodeHome")) {
                                parents = {season, show};
                            } else if (id == QStringLiteral("seasonHome")) {
                                parents = {show};
                            }
                            parents.push_back(
                                makeItem(id == QStringLiteral("movieHome") ? QStringLiteral("moviesLibrary")
                                                                           : QStringLiteral("showsLibrary"),
                                         QStringLiteral("Library"), QStringLiteral("CollectionFolder"), true));
                            parents.push_back(makeItem(QStringLiteral("serverRoot"), QStringLiteral("Root"),
                                                       QStringLiteral("Folder"), true));
                            if (delayAncestors) {
                                delayedAncestors.push_back({socket, QJsonDocument(parents).toJson()});
                            } else {
                                respond(socket, QJsonDocument(parents).toJson(), 200,
                                        "Content-Type: application/json\r\n");
                            }
                            return;
                        }
                        QJsonArray values;
                        bool feed = false;
                        auto query = QUrlQuery(request.url);
                        if (path.endsWith(QStringLiteral("/Resume"))) {
                            if (failResume) {
                                respond(socket, {}, 503);
                                return;
                            }
                            values = {episode};
                            feed = true;
                        } else if (path.endsWith(QStringLiteral("/Views"))) {
                            values = {
                                makeItem(QStringLiteral("moviesLibrary"), QStringLiteral("Movies"),
                                         QStringLiteral("CollectionFolder"), true),
                                makeItem(groupedViews ? QStringLiteral("groupedShows") : QStringLiteral("showsLibrary"),
                                         QStringLiteral("Shows"), QStringLiteral("CollectionFolder"), true)};
                        } else if (path.contains(QStringLiteral("/Users/abc123/Items/"))) {
                            respondJson(socket, movie);
                            return;
                        } else if (query.hasQueryItem(QStringLiteral("ParentId"))) {
                            auto parent = query.queryItemValue(QStringLiteral("ParentId"));
                            if (parent == QStringLiteral("showsLibrary")) {
                                values = {show};
                            } else if (parent == QStringLiteral("showHome")) {
                                values = {season};
                            } else if (parent == QStringLiteral("seasonHome")) {
                                values = {episode};
                            } else {
                                values = {movie};
                                auto second = movie;
                                second[QStringLiteral("Id")] = QStringLiteral("movieOther");
                                second[QStringLiteral("Name")] =
                                    QStringLiteral("Another Story with a Very Long Title for the Poster Gallery");
                                second[QStringLiteral("ImageTags")] = QJsonObject{};
                                values.push_back(second);
                            }
                        } else {
                            values =
                                query.queryItemValue(QStringLiteral("IncludeItemTypes")) == QStringLiteral("Series")
                                    ? QJsonArray{show}
                                    : QJsonArray{movie};
                            feed = true;
                        }
                        QJsonObject response{{QStringLiteral("Items"), values},
                                             {QStringLiteral("TotalRecordCount"), values.size()}};
                        if (delayFeeds && feed) {
                            delayedFeeds.push_back({socket, response});
                        } else {
                            respondJson(socket, response);
                        }
                        return;
                    }
                    if (path.endsWith(QStringLiteral("/Resume"))) {
                        respondJson(socket, {{QStringLiteral("Items"), QJsonArray{}}});
                        return;
                    }
                    if (path.endsWith(QStringLiteral("/Views")) || path.endsWith(QStringLiteral("/Items"))) {
                        bool library = path.endsWith(QStringLiteral("/Views"));
                        QJsonObject item{{QStringLiteral("Id"), QStringLiteral("item123")},
                                         {QStringLiteral("Name"), QStringLiteral("Example")},
                                         {QStringLiteral("Overview"), QStringLiteral("A show description.")},
                                         {QStringLiteral("Type"),
                                          library ? QStringLiteral("CollectionFolder") : QStringLiteral("Movie")},
                                         {QStringLiteral("IsFolder"), library},
                                         {QStringLiteral("RunTimeTicks"), 100000000},
                                         {QStringLiteral("UserData"),
                                          QJsonObject{{QStringLiteral("Played"), false},
                                                      {QStringLiteral("PlaybackPositionTicks"), 25000000}}}};
                        QJsonArray values{item};
                        if (!library) {
                            item[QStringLiteral("Id")] = QStringLiteral("item456");
                            item[QStringLiteral("Name")] = QStringLiteral("Second item");
                            values.push_back(item);
                        }
                        respondJson(socket, {{QStringLiteral("Items"), values},
                                             {QStringLiteral("TotalRecordCount"), values.size()}});
                        return;
                    }
                    respondJson(socket, {});
                });
            }
        });
    }
    QUrl base() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/jellyfin").arg(serverPort())); }
    QUrl stream() const {
        return QUrl(base().toString() + QStringLiteral("/Videos/item123/stream?api_key=test-token"));
    }
    static void respond(QTcpSocket* socket, QByteArray const& body, int code = 200, QByteArray headers = {}) {
        socket->write("HTTP/1.1 " + QByteArray::number(code) + " OK\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\n" + headers + "\r\n" + body);
        socket->disconnectFromHost();
    }
    static void respondJson(QTcpSocket* socket, QJsonObject const& object) {
        respond(socket, QJsonDocument(object).toJson(QJsonDocument::Compact));
    }
    void releaseMedia() {
        pauseMedia = false;
        for (auto const& [socket, body] : std::exchange(pausedMedia, {})) {
            if (socket) {
                socket->write(body);
                socket->disconnectFromHost();
            }
        }
    }
    void releaseAncestors() {
        for (auto const& [socket, body] : std::exchange(delayedAncestors, {})) {
            if (socket) {
                respond(socket, body, 200, "Content-Type: application/json\r\n");
            }
        }
    }
    void releaseFeeds() {
        for (auto const& [socket, body] : std::exchange(delayedFeeds, {})) {
            if (socket) {
                respondJson(socket, body);
            }
        }
    }
    void releaseStops() {
        for (auto socket : std::exchange(delayedStops, {})) {
            if (socket) {
                respondJson(socket, {});
            }
        }
    }
};
