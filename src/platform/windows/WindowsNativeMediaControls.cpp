#include "platform/NativeMediaControls.h"

#include <algorithm>
#include <limits>
#include <mutex>

#include <QPlatformSurfaceEvent>
#include <QThread>
#include <QTimer>
#include <QWindow>

#include <qt_windows.h>
#include <roapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>

#include "diagnostics/LogCategories.h"
#include "playback/MediaSession.h"

using namespace winrt::Windows::Media;
using winrt::Windows::Foundation::TimeSpan;

namespace {
void reportFailure(char const* operation, winrt::hresult_error const& error) {
    // Only a disconnected or administratively unavailable system service is
    // recoverable. Unknown failures must not conceal an adapter/SDK defect.
    HRESULT const code = error.code();
    if (code != RPC_E_DISCONNECTED && code != RPC_E_SERVER_DIED && code != RPC_E_SERVER_DIED_DNE &&
        code != HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) && code != HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE) &&
        code != HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED)) {
        qFatal("Windows media controls %s failed unexpectedly: 0x%08X %ls", operation,
               static_cast<unsigned int>(error.code().value), error.message().c_str());
    }
    qCWarning(sunplayerLogPlatform, "Windows media controls %s failed: 0x%08X %ls", operation,
              static_cast<unsigned int>(error.code().value), error.message().c_str());
}

// Native event removal does not join callbacks already running on another thread.
// The gate protects the QObject until enqueueing finishes; deleting that QObject
// subsequently discards queued work before either dependency can be destroyed.
struct CommandReceiver {
    std::mutex mutex;
    QObject* object = nullptr;
};

class WindowsNativeMediaControls final : public QObject, public NativeMediaControls {
  public:
    WindowsNativeMediaControls(MediaSession& session, QWindow& window) : m_session(session), m_window(window) {
        Q_ASSERT(thread() == session.thread());
        Q_ASSERT(thread() == window.thread());
        HRESULT const result = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
            qFatal("Could not initialize the Windows Runtime for media controls: 0x%08X",
                   static_cast<unsigned int>(result));
        }
        m_ownsRuntime = SUCCEEDED(result);
        m_window.installEventFilter(this);
        connect(&session, &MediaSession::sessionChanged, this, [this] { refresh(); });
        connect(&session, &MediaSession::timelineChanged, this, [this] {
            if (m_duration != m_session.durationMilliseconds() || m_seekable != m_session.seekable()) {
                refresh();
            }
        });
        connect(&session, &MediaSession::positionJumped, this, [this](qlonglong) { refresh(); });
        m_refreshTimer.setInterval(5000);
        connect(&m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });
        // winId may synchronously deliver SurfaceCreated, which also calls attach.
        m_window.winId();
        attach();
    }

    ~WindowsNativeMediaControls() override {
        Q_ASSERT(QThread::currentThread() == thread());
        m_window.removeEventFilter(this);
        detach();
        if (m_ownsRuntime) {
            RoUninitialize();
        }
    }

  private:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == &m_window && event->type() == QEvent::PlatformSurface) {
            auto const* surface = static_cast<QPlatformSurfaceEvent*>(event);
            if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
                detach();
            } else if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
                attach();
            }
        }
        return QObject::eventFilter(watched, event);
    }

    void attach() {
        if (m_controls) {
            return;
        }
        try {
            auto const factory =
                winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
            winrt::check_hresult(factory->GetForWindow(reinterpret_cast<HWND>(m_window.winId()),
                                                       winrt::guid_of<SystemMediaTransportControls>(),
                                                       winrt::put_abi(m_controls)));
            m_callbackObject = std::make_unique<QObject>();
            m_receiver = std::make_shared<CommandReceiver>();
            m_receiver->object = m_callbackObject.get();
            auto const receiver = m_receiver;
            auto* session = &m_session;
            m_buttonToken = m_controls.ButtonPressed(
                [receiver, session](SystemMediaTransportControls const&,
                                    SystemMediaTransportControlsButtonPressedEventArgs const& args) {
                    try {
                        auto const button = args.Button();
                        std::lock_guard lock(receiver->mutex);
                        if (!receiver->object) {
                            return;
                        }
                        bool const queued = QMetaObject::invokeMethod(
                            receiver->object,
                            [session, button] {
                                bool const controllable =
                                    session->state() == MediaSession::State::Ready || session->seeking();
                                switch (button) {
                                case SystemMediaTransportControlsButton::Play:
                                    if (controllable) {
                                        session->play();
                                    }
                                    break;
                                case SystemMediaTransportControlsButton::Pause:
                                    if (controllable) {
                                        session->pause();
                                    }
                                    break;
                                case SystemMediaTransportControlsButton::Stop:
                                    session->stop();
                                    break;
                                default:
                                    break;
                                }
                            },
                            Qt::QueuedConnection);
                        if (!queued) {
                            qFatal("Could not queue a Windows media command");
                        }
                    } catch (winrt::hresult_error const& error) { reportFailure("read button request", error); }
                });
            m_hasButtonToken = true;
            m_positionToken = m_controls.PlaybackPositionChangeRequested(
                [receiver, session](SystemMediaTransportControls const&,
                                    PlaybackPositionChangeRequestedEventArgs const& args) {
                    try {
                        auto const ticks = args.RequestedPlaybackPosition().count();
                        if (ticks < 0) {
                            return;
                        }
                        std::lock_guard lock(receiver->mutex);
                        if (!receiver->object) {
                            return;
                        }
                        bool const queued = QMetaObject::invokeMethod(
                            receiver->object,
                            [session, ticks] {
                                auto const milliseconds = ticks / 10000;
                                auto const duration = session->durationMilliseconds();
                                if (!session->seekable() || milliseconds > duration ||
                                    (milliseconds == duration && ticks % 10000 != 0)) {
                                    return;
                                }
                                session->seekToMilliseconds(milliseconds);
                            },
                            Qt::QueuedConnection);
                        if (!queued) {
                            qFatal("Could not queue a Windows media command");
                        }
                    } catch (winrt::hresult_error const& error) { reportFailure("read position request", error); }
                });
            m_hasPositionToken = true;
            m_metadataItem = 0;
            m_title.clear();
            refresh();
        } catch (winrt::hresult_error const& error) {
            reportFailure("attach", error);
            detach();
        }
    }

    void detach() {
        m_refreshTimer.stop();
        if (m_receiver) {
            std::lock_guard lock(m_receiver->mutex);
            m_receiver->object = nullptr;
        }
        m_callbackObject.reset();
        m_receiver.reset();
        if (m_controls) {
            // Revoke independently: one external failure must not skip the other subscription.
            if (m_hasButtonToken) {
                try {
                    m_controls.ButtonPressed(m_buttonToken);
                } catch (winrt::hresult_error const& error) { reportFailure("remove button handler", error); }
            }
            if (m_hasPositionToken) {
                try {
                    m_controls.PlaybackPositionChangeRequested(m_positionToken);
                } catch (winrt::hresult_error const& error) { reportFailure("remove position handler", error); }
            }
            try {
                m_controls.IsEnabled(false);
                m_controls.DisplayUpdater().ClearAll();
                m_controls.DisplayUpdater().Update();
            } catch (winrt::hresult_error const& error) { reportFailure("clear", error); }
        }
        m_hasButtonToken = false;
        m_hasPositionToken = false;
        m_controls = nullptr;
    }

    void refresh() {
        if (!m_controls) {
            return;
        }
        try {
            bool const hasItem = m_session.mediaItemId() != 0 && m_session.state() != MediaSession::State::Error;
            bool const controllable =
                hasItem && (m_session.state() == MediaSession::State::Ready || m_session.seeking());
            m_controls.IsPlayEnabled(controllable);
            m_controls.IsPauseEnabled(controllable);
            m_controls.IsStopEnabled(hasItem);
            m_controls.IsNextEnabled(false);
            m_controls.IsPreviousEnabled(false);
            m_controls.IsFastForwardEnabled(false);
            m_controls.IsRewindEnabled(false);
            m_controls.IsEnabled(hasItem);
            auto const clock = m_session.mediaClockSnapshot();
            auto const status = !hasItem                                   ? MediaPlaybackStatus::Closed
                                : m_session.stopped() || m_session.ended() ? MediaPlaybackStatus::Stopped
                                : !m_session.playRequested()               ? MediaPlaybackStatus::Paused
                                : clock.advancing                          ? MediaPlaybackStatus::Playing
                                                                           : MediaPlaybackStatus::Changing;
            m_controls.PlaybackStatus(status);
            std::uint64_t const item = hasItem ? m_session.mediaItemId() : 0;
            QString const title = hasItem ? m_session.displayName() : QString{};
            if (item != m_metadataItem || title != m_title) {
                auto const updater = m_controls.DisplayUpdater();
                updater.ClearAll();
                if (hasItem) {
                    updater.Type(MediaPlaybackType::Video);
                    updater.VideoProperties().Title(winrt::hstring(reinterpret_cast<wchar_t const*>(title.utf16()),
                                                                   static_cast<std::uint32_t>(title.size())));
                }
                updater.Update();
                m_metadataItem = item;
                m_title = title;
            }
            m_duration = m_session.durationMilliseconds();
            m_seekable = m_session.seekable();
            SystemMediaTransportControlsTimelineProperties timeline;
            // Windows TimeSpan uses signed 100-nanosecond ticks.
            constexpr auto maxTicks = std::numeric_limits<std::int64_t>::max();
            auto const durationTicks = hasItem && m_duration > 0 ? std::min(m_duration, maxTicks / 10000) * 10000 : 0;
            auto const positionTicks =
                std::min(std::min(clock.positionMicroseconds, maxTicks / 10) * 10, durationTicks);
            timeline.StartTime(TimeSpan{0});
            timeline.EndTime(TimeSpan{durationTicks});
            timeline.Position(TimeSpan{positionTicks});
            timeline.MinSeekTime(TimeSpan{m_seekable ? 0 : positionTicks});
            timeline.MaxSeekTime(TimeSpan{m_seekable ? durationTicks : positionTicks});
            m_controls.UpdateTimelineProperties(timeline);
            if (hasItem && m_session.playRequested() && !m_session.ended() && !m_refreshTimer.isActive()) {
                m_refreshTimer.start();
            } else if (!hasItem || !m_session.playRequested() || m_session.ended()) {
                m_refreshTimer.stop();
            }
        } catch (winrt::hresult_error const& error) {
            reportFailure("publish", error);
            // Surface recreation is the bounded retry trigger after an unavailable native service.
            detach();
        }
    }

    MediaSession& m_session;
    QWindow& m_window;
    QTimer m_refreshTimer;
    bool m_ownsRuntime = false;
    SystemMediaTransportControls m_controls{nullptr};
    winrt::event_token m_buttonToken{};
    winrt::event_token m_positionToken{};
    bool m_hasButtonToken = false;
    bool m_hasPositionToken = false;
    std::shared_ptr<CommandReceiver> m_receiver;
    std::unique_ptr<QObject> m_callbackObject;
    std::uint64_t m_metadataItem = 0;
    QString m_title;
    qlonglong m_duration = 0;
    bool m_seekable = false;
};
} // namespace

std::unique_ptr<NativeMediaControls> createNativeMediaControls(MediaSession& session, QWindow& window) {
    return std::make_unique<WindowsNativeMediaControls>(session, window);
}
