#include "platform/NativeMediaControls.h"

#include <algorithm>
#include <cmath>
#include <vector>

#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include <QThread>

#include "playback/MediaSession.h"

enum class RemoteAction { Play, Pause, Toggle, Stop, Position, SkipBackward, SkipForward };

// Native handlers may already be in flight when their registrations are removed.
// Resolve this weak target only on the main queue, where teardown also runs.
@interface SunPlayerRemoteCommandTarget : NSObject
@property(nonatomic, copy) void (^execute)(RemoteAction, double);
@end

@implementation SunPlayerRemoteCommandTarget
@end

namespace {
class MacNativeMediaControls final : public QObject, public NativeMediaControls {
  public:
    explicit MacNativeMediaControls(MediaSession& session) : m_session(session) {
        Q_ASSERT([NSThread isMainThread]);
        Q_ASSERT(session.thread() == QThread::currentThread());
        m_target = [SunPlayerRemoteCommandTarget new];
        m_target.execute = ^(RemoteAction action, double seconds) { execute(action, seconds); };
        registerCommand(m_commands.playCommand, RemoteAction::Play);
        registerCommand(m_commands.pauseCommand, RemoteAction::Pause);
        registerCommand(m_commands.togglePlayPauseCommand, RemoteAction::Toggle);
        registerCommand(m_commands.stopCommand, RemoteAction::Stop);
        registerCommand(m_commands.changePlaybackPositionCommand, RemoteAction::Position);
        registerCommand(m_commands.skipBackwardCommand, RemoteAction::SkipBackward);
        registerCommand(m_commands.skipForwardCommand, RemoteAction::SkipForward);
        m_commands.skipBackwardCommand.preferredIntervals = @[ @10 ];
        m_commands.skipForwardCommand.preferredIntervals = @[ @10 ];
        for (MPRemoteCommand* command in @[
                 m_commands.nextTrackCommand, m_commands.previousTrackCommand, m_commands.seekForwardCommand,
                 m_commands.seekBackwardCommand, m_commands.changePlaybackRateCommand,
                 m_commands.changeRepeatModeCommand, m_commands.changeShuffleModeCommand, m_commands.ratingCommand,
                 m_commands.likeCommand, m_commands.dislikeCommand, m_commands.bookmarkCommand,
                 m_commands.enableLanguageOptionCommand, m_commands.disableLanguageOptionCommand
             ]) {
            command.enabled = NO;
        }
        connect(&session, &MediaSession::sessionChanged, this, [this] { reconcile(); });
        connect(&session, &MediaSession::timelineChanged, this, [this] { reconcile(); });
        connect(&session, &MediaSession::mediaTracksChanged, this, [this] { reconcile(); });
        connect(&session, &MediaSession::positionJumped, this, [this] { reconcile(true); });
        reconcile();
    }

    ~MacNativeMediaControls() override {
        Q_ASSERT([NSThread isMainThread]);
        m_target.execute = nil;
        for (auto const& registration : m_registrations) {
            registration.command.enabled = NO;
            [registration.command removeTarget:registration.target];
        }
        m_target = nil;
        m_info.nowPlayingInfo = nil;
        m_info.playbackState = MPNowPlayingPlaybackStateUnknown;
    }

  private:
    struct Registration {
        MPRemoteCommand* command;
        id target;
    };

    void registerCommand(MPRemoteCommand* command, RemoteAction action) {
        __weak SunPlayerRemoteCommandTarget* const weakTarget = m_target;
        id target = [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
          double seconds = 0;
          if (action == RemoteAction::Position) {
              if (![event isKindOfClass:[MPChangePlaybackPositionCommandEvent class]]) {
                  return MPRemoteCommandHandlerStatusCommandFailed;
              }
              seconds = ((MPChangePlaybackPositionCommandEvent*)event).positionTime;
          } else if (action == RemoteAction::SkipBackward || action == RemoteAction::SkipForward) {
              if (![event isKindOfClass:[MPSkipIntervalCommandEvent class]]) {
                  return MPRemoteCommandHandlerStatusCommandFailed;
              }
              seconds = ((MPSkipIntervalCommandEvent*)event).interval;
          }
          if (!std::isfinite(seconds) || seconds < 0) {
              return MPRemoteCommandHandlerStatusCommandFailed;
          }
          dispatch_async(dispatch_get_main_queue(), ^{
            SunPlayerRemoteCommandTarget* const target = weakTarget;
            if (target.execute) {
                target.execute(action, seconds);
            }
          });
          return MPRemoteCommandHandlerStatusSuccess;
        }];
        if (!target) {
            qFatal("macOS failed to register a native media command handler");
        }
        m_registrations.push_back({command, target});
    }

    void execute(RemoteAction action, double seconds) {
        Q_ASSERT(QThread::currentThread() == m_session.thread());
        if (m_session.mediaItemId() == 0 || m_session.state() == MediaSession::State::Error) {
            return;
        }
        if (action == RemoteAction::Stop) {
            m_session.stop();
            return;
        }
        if (action == RemoteAction::Position || action == RemoteAction::SkipBackward ||
            action == RemoteAction::SkipForward) {
            if (!m_session.seekable()) {
                return;
            }
            long double position = static_cast<long double>(seconds) * 1000;
            long double const duration = m_session.durationMilliseconds();
            if (action == RemoteAction::Position) {
                if (position > duration) {
                    return;
                }
            } else {
                position =
                    m_session.positionMilliseconds() + (action == RemoteAction::SkipBackward ? -position : position);
                position = std::clamp(position, 0.0L, duration);
            }
            m_session.seekToMilliseconds(static_cast<qlonglong>(position));
            return;
        }
        if (m_session.state() != MediaSession::State::Ready && !m_session.seeking()) {
            return;
        }
        switch (action) {
        case RemoteAction::Play:
            m_session.play();
            break;
        case RemoteAction::Pause:
            m_session.pause();
            break;
        case RemoteAction::Toggle:
            m_session.togglePlayback();
            break;
        default:
            Q_UNREACHABLE();
        }
    }

    void reconcile(bool positionJumped = false) {
        bool const hasItem = m_session.mediaItemId() != 0 && m_session.state() != MediaSession::State::Empty &&
                             m_session.state() != MediaSession::State::Error;
        bool const transport = hasItem && (m_session.state() == MediaSession::State::Ready || m_session.seeking());
        m_commands.playCommand.enabled = transport;
        m_commands.pauseCommand.enabled = transport;
        m_commands.togglePlayPauseCommand.enabled = transport;
        m_commands.stopCommand.enabled = hasItem;
        m_commands.changePlaybackPositionCommand.enabled = hasItem && m_session.seekable();
        m_commands.skipBackwardCommand.enabled = hasItem && m_session.seekable();
        m_commands.skipForwardCommand.enabled = hasItem && m_session.seekable();
        if (!hasItem) {
            m_info.nowPlayingInfo = nil;
            m_info.playbackState = MPNowPlayingPlaybackStateUnknown;
            m_metadata = nil;
            m_itemId = 0;
            return;
        }

        auto const clock = m_session.mediaClockSnapshot();
        MPNowPlayingPlaybackState const state = m_session.stopped() || m_session.ended()
                                                    ? MPNowPlayingPlaybackStateStopped
                                                : m_session.playRequested() ? MPNowPlayingPlaybackStatePlaying
                                                                            : MPNowPlayingPlaybackStatePaused;
        QString const title = m_session.displayName();
        NSString* const nativeTitle = [NSString stringWithCharacters:reinterpret_cast<unichar const*>(title.utf16())
                                                              length:static_cast<NSUInteger>(title.size())];
        NSMutableDictionary* const metadata = [@{
            MPMediaItemPropertyTitle : nativeTitle,
            MPNowPlayingInfoPropertyMediaType :
                @(m_session.selectedVideoStreamIndex() >= 0 ? MPNowPlayingInfoMediaTypeVideo
                                                            : MPNowPlayingInfoMediaTypeAudio),
            MPNowPlayingInfoPropertyPlaybackRate : @(clock.advancing ? 1.0 : 0.0),
            MPNowPlayingInfoPropertyDefaultPlaybackRate : @1.0
        } mutableCopy];
        if (m_session.durationMilliseconds() > 0) {
            metadata[MPMediaItemPropertyPlaybackDuration] = @(m_session.durationMilliseconds() / 1000.0);
        }
        // The system extrapolates elapsed time. Re-anchor on semantic changes or
        // explicit jumps, rather than rewriting it on every ordinary timeline tick.
        if (positionJumped || m_itemId != m_session.mediaItemId() || ![m_metadata isEqualToDictionary:metadata] ||
            m_info.playbackState != state) {
            m_metadata = [metadata copy];
            m_itemId = m_session.mediaItemId();
            metadata[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(clock.positionMicroseconds / 1000000.0);
            m_info.nowPlayingInfo = metadata;
            m_info.playbackState = state;
        }
    }

    MediaSession& m_session;
    MPNowPlayingInfoCenter* m_info = [MPNowPlayingInfoCenter defaultCenter];
    MPRemoteCommandCenter* m_commands = [MPRemoteCommandCenter sharedCommandCenter];
    SunPlayerRemoteCommandTarget* m_target = nil;
    std::vector<Registration> m_registrations;
    NSDictionary* m_metadata = nil;
    std::uint64_t m_itemId = 0;
};
} // namespace

std::unique_ptr<NativeMediaControls> createNativeMediaControls(MediaSession& session, QWindow&) {
    return std::make_unique<MacNativeMediaControls>(session);
}
