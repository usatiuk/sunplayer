#include "platform/PlaybackPowerInhibitor.h"

#include <memory>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPointer>
#include <QUuid>
#include <QVariantMap>

#include "diagnostics/LogCategories.h"

namespace {
constexpr auto portalService = "org.freedesktop.portal.Desktop";
constexpr auto portalPath = "/org/freedesktop/portal/desktop";
constexpr auto inhibitInterface = "org.freedesktop.portal.Inhibit";
constexpr auto requestInterface = "org.freedesktop.portal.Request";
constexpr quint32 suspendFlag = 4;
constexpr quint32 idleFlag = 8;

struct PortalRequestState {
    bool ownerAlive = true;
    bool desired = false;
    bool methodReturned = false;
    bool responseCompleted = false;
    bool acquired = false;
    bool responseSubscribed = false;
    bool restartIfRequestDoesNotAcquire = false;
    QString handle;
    QPointer<QDBusPendingCallWatcher> pending;
    QPointer<QObject> receiver;
};

QString expectedRequestPath(QString const& token) {
    QString sender = QDBusConnection::sessionBus().baseService();
    if (sender.startsWith(u':')) {
        sender.remove(0, 1);
    }
    sender.replace(u'.', u'_');
    return sender.isEmpty() ? QString{}
                            : QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

bool closeRequest(QString const& path) {
    if (path.isEmpty()) {
        return true;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService), path, QString::fromLatin1(requestInterface), QStringLiteral("Close"));
    if (QDBusConnection::sessionBus().send(message)) {
        return true;
    }
    qCWarning(sunplayerLogPlatform, "Could not send the Linux playback power-inhibition close request");
    return false;
}

void disconnectResponse(PortalRequestState& state) {
    if (!state.responseSubscribed || !state.receiver) {
        state.responseSubscribed = false;
        return;
    }
    QDBusConnection::sessionBus().disconnect(QString::fromLatin1(portalService), state.handle,
                                             QString::fromLatin1(requestInterface), QStringLiteral("Response"),
                                             state.receiver, SLOT(portalResponse(uint, QVariantMap, QDBusMessage)));
    state.responseSubscribed = false;
}

bool subscribeResponse(PortalRequestState& state) {
    if (!state.receiver || state.handle.isEmpty()) {
        return false;
    }
    state.responseSubscribed = QDBusConnection::sessionBus().connect(
        QString::fromLatin1(portalService), state.handle, QString::fromLatin1(requestInterface),
        QStringLiteral("Response"), state.receiver, SLOT(portalResponse(uint, QVariantMap, QDBusMessage)));
    return state.responseSubscribed;
}

void clearHandle(PortalRequestState& state, bool responseCompleted) {
    disconnectResponse(state);
    state.handle.clear();
    state.methodReturned = false;
    state.responseCompleted = responseCompleted;
    state.acquired = false;
}

void closeKnownRequest(PortalRequestState& state) {
    bool const acquired = state.acquired;
    QString const handle = state.handle;
    clearHandle(state, true);
    closeRequest(handle);
    if (acquired) {
        qCDebug(sunplayerLogPlatform, "Released the Linux playback power inhibition");
    }
}
} // namespace

class LinuxPlaybackPowerInhibitor final : public QObject, public PlaybackPowerInhibitor {
    Q_OBJECT

  public:
    LinuxPlaybackPowerInhibitor() { m_state->receiver = this; }

    ~LinuxPlaybackPowerInhibitor() override {
        m_state->ownerAlive = false;
        m_state->desired = false;
        if (!m_state->handle.isEmpty()) {
            closeKnownRequest(*m_state);
        }
        m_state->receiver.clear();
    }

  protected:
    void setActive(bool active) override {
        m_state->desired = active;
        if (!active) {
            m_state->restartIfRequestDoesNotAcquire = false;
            if (m_state->methodReturned || m_state->acquired) {
                closeKnownRequest(*m_state);
            }
            return;
        }
        if (m_state->pending) {
            m_state->restartIfRequestDoesNotAcquire = true;
            return;
        }
        if (!m_state->handle.isEmpty()) {
            return;
        }
        beginRequest();
    }

  private slots:
    void portalResponse(uint response, QVariantMap const&, QDBusMessage const& message) {
        if (message.path() != m_state->handle) {
            return;
        }

        m_state->responseCompleted = true;
        disconnectResponse(*m_state);
        if (response != 0) {
            bool const restart =
                m_state->restartIfRequestDoesNotAcquire && m_state->ownerAlive && m_state->desired && !m_state->pending;
            if (!m_state->pending) {
                m_state->restartIfRequestDoesNotAcquire = false;
            }
            if (m_state->ownerAlive && m_state->desired) {
                qCWarning(sunplayerLogPlatform, "The Linux playback power-inhibition request failed: response=%u",
                          response);
            }
            closeKnownRequest(*m_state);
            if (restart) {
                beginRequest();
            }
            return;
        }

        m_state->acquired = true;
        if (m_state->ownerAlive && m_state->desired) {
            m_state->restartIfRequestDoesNotAcquire = false;
            qCDebug(sunplayerLogPlatform, "Acquired the Linux playback power inhibition");
        } else {
            closeKnownRequest(*m_state);
        }
    }

  private:
    void beginRequest() {
        QDBusConnection const bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) {
            qCWarning(sunplayerLogPlatform, "The session D-Bus is unavailable for playback power inhibition");
            return;
        }

        QString const token = QStringLiteral("sunplayer_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        m_state->handle = expectedRequestPath(token);
        m_state->methodReturned = false;
        m_state->responseCompleted = false;
        m_state->acquired = false;
        m_state->restartIfRequestDoesNotAcquire = false;
        if (!subscribeResponse(*m_state)) {
            qCWarning(sunplayerLogPlatform, "Could not subscribe to the Linux playback power-inhibition response");
            m_state->handle.clear();
            return;
        }

        QVariantMap options;
        options.insert(QStringLiteral("handle_token"), token);
        options.insert(QStringLiteral("reason"),
                       QCoreApplication::translate("PlaybackPowerInhibitor", "Playing media"));
        QDBusMessage message =
            QDBusMessage::createMethodCall(QString::fromLatin1(portalService), QString::fromLatin1(portalPath),
                                           QString::fromLatin1(inhibitInterface), QStringLiteral("Inhibit"));
        message.setArguments({QVariant::fromValue(QString{}), QVariant::fromValue(suspendFlag | idleFlag),
                              QVariant::fromValue(options)});

        auto* const watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), QCoreApplication::instance());
        m_state->pending = watcher;
        std::shared_ptr<PortalRequestState> const state = m_state;
        QPointer<LinuxPlaybackPowerInhibitor> const owner = this;
        QObject::connect(
            watcher, &QDBusPendingCallWatcher::finished, watcher, [state, owner](QDBusPendingCallWatcher* finished) {
                if (state->pending == finished) {
                    state->pending.clear();
                }
                auto const finishRequest = [&] {
                    bool const restart = state->restartIfRequestDoesNotAcquire && state->ownerAlive && state->desired &&
                                         state->handle.isEmpty();
                    if (restart) {
                        state->restartIfRequestDoesNotAcquire = false;
                    }
                    finished->deleteLater();
                    if (restart && owner) {
                        owner->beginRequest();
                    }
                };
                QDBusPendingReply<QDBusObjectPath> const reply = *finished;
                if (reply.isError()) {
                    if (!state->responseCompleted && state->ownerAlive && state->desired) {
                        qCWarning(sunplayerLogPlatform, "Could not request Linux playback power inhibition: %s",
                                  qUtf8Printable(reply.error().message()));
                    }
                    if (!state->responseCompleted) {
                        clearHandle(*state, false);
                    }
                    finishRequest();
                    return;
                }

                QString const returnedHandle = reply.value().path();
                state->methodReturned = true;
                if (state->responseCompleted) {
                    if (!state->ownerAlive) {
                        closeRequest(returnedHandle);
                    }
                    finishRequest();
                    return;
                }
                if (!state->ownerAlive || !state->desired) {
                    disconnectResponse(*state);
                    closeRequest(returnedHandle);
                    state->handle.clear();
                    finishRequest();
                    return;
                }
                if (returnedHandle != state->handle) {
                    disconnectResponse(*state);
                    state->handle = returnedHandle;
                    if (!subscribeResponse(*state)) {
                        qCWarning(sunplayerLogPlatform,
                                  "Could not follow the Linux playback power-inhibition response");
                        closeRequest(returnedHandle);
                        state->handle.clear();
                    }
                }
                finishRequest();
            });
    }

    std::shared_ptr<PortalRequestState> m_state = std::make_shared<PortalRequestState>();
};

std::unique_ptr<PlaybackPowerInhibitor> createPlaybackPowerInhibitor() {
    return std::make_unique<LinuxPlaybackPowerInhibitor>();
}

#include "LinuxPlaybackPowerInhibitor.moc"
