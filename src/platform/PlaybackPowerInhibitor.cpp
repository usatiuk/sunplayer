#include "platform/PlaybackPowerInhibitor.h"

#include "playback/MediaSession.h"

void PlaybackPowerInhibitor::reconcile(MediaSession const& session) {
    bool const activeMedia =
        session.state() == MediaSession::State::Opening || session.state() == MediaSession::State::Ready;
    bool const active = activeMedia && session.playRequested();
    if (active == m_active) {
        return;
    }

    m_active = active;
    setActive(active);
}
