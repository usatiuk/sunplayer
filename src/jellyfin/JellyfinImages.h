#pragma once
#include "jellyfin/JellyfinClient.h"
#include <QPointer>
#include <QQuickAsyncImageProvider>

class JellyfinImages final : public QQuickAsyncImageProvider {
  public:
    explicit JellyfinImages(JellyfinClient& client) : m_client(&client) {}
    QQuickImageResponse* requestImageResponse(QString const& id, QSize const& requestedSize) override;

  private:
    QPointer<JellyfinClient> m_client;
};
