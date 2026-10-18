#include "jellyfin/JellyfinImages.h"
#include <QBuffer>
#include <QImageReader>
#include <QQuickTextureFactory>

namespace {
class JellyfinImageResponse final : public QQuickImageResponse {
  public:
    QImage image;
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image);
    }
    void complete(QImage value = {}) {
        image = std::move(value);
        emit finished();
    }
};
} // namespace
QQuickImageResponse* JellyfinImages::requestImageResponse(QString const& id, QSize const&) {
    auto* response = new JellyfinImageResponse;
    QPointer<JellyfinImageResponse> guard(response);
    auto client = m_client;
    if (!client) {
        QMetaObject::invokeMethod(
            response,
            [guard] {
                if (guard) {
                    guard->complete();
                }
            },
            Qt::QueuedConnection);
        return response;
    }
    QMetaObject::invokeMethod(
        client,
        [client, guard, id] {
            if (!guard) {
                return;
            }
            auto const parts = id.split(u'/');
            if (!client || !client->authenticated() || parts.size() != 3) {
                guard->complete();
                return;
            }
            // IDs originate in validated API values. Requests and token access stay
            // on the client's thread, while Qt retains image loading/caching policy.
            auto* reply = client->image(parts[0], parts[2], parts[1]);
            if (!reply) {
                guard->complete();
                return;
            }
            QObject::connect(reply, &QNetworkReply::finished, client,
                             [guard, reply, backdrop = parts[1] == QStringLiteral("Backdrop")] {
                                 reply->deleteLater();
                                 if (!guard) {
                                     return;
                                 }
                                 QImage image;
                                 if (reply->error() == QNetworkReply::NoError &&
                                     reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
                                     QByteArray bytes = reply->readAll();
                                     QBuffer buffer(&bytes);
                                     buffer.open(QIODevice::ReadOnly);
                                     QImageReader reader(&buffer);
                                     QSize const size = reader.size();
                                     if (size.isValid() && size.width() <= 8192 && size.height() <= 8192) {
                                         reader.setScaledSize(size.scaled(backdrop ? QSize(1280, 720) : QSize(480, 720),
                                                                          Qt::KeepAspectRatio));
                                         image = reader.read();
                                     }
                                 }
                                 guard->complete(std::move(image));
                             });
        },
        Qt::QueuedConnection);
    return response;
}
