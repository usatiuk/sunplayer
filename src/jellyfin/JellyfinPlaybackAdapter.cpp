#include "jellyfin/JellyfinPlaybackAdapter.h"

JellyfinPlaybackAdapter::JellyfinPlaybackAdapter(JellyfinBrowser& browser, MediaSession& player, QObject* parent)
    : QObject(parent), m_browser(browser), m_player(player) {
    // Tear down the old occurrence before the browser can accept another login.
    // Pending reports keep login busy until their old-session callbacks settle.
    connect(&browser.client(), &JellyfinClient::authenticationLost, this, &JellyfinPlaybackAdapter::close);
    connect(&browser, &JellyfinBrowser::playRequested, this, &JellyfinPlaybackAdapter::play);
    connect(&player, &MediaSession::mediaClosing, this, [this](qulonglong id, qlonglong position) {
        ++m_resolution;
        if (!pending()) {
            m_browser.setSyncPending(false);
        }
        if (m_playback && id == m_mediaId) {
            finish(position);
            m_playback.reset();
            m_mediaId = 0;
        }
    });
    connect(&player, &MediaSession::sessionChanged, this, &JellyfinPlaybackAdapter::observe);
    connect(&player, &MediaSession::playbackFinishing, this, [this](qulonglong id, qlonglong position) {
        if (id == m_mediaId) {
            finish(position);
        }
    });
    connect(&browser, &JellyfinBrowser::watchedRequested, this, [this](QString id, bool watched) {
        if (m_playback && m_playback->itemId == id) {
            close();
        }
        if (!pending()) {
            m_browser.applyWatched(id, watched);
        } else {
            connect(
                this, &JellyfinPlaybackAdapter::settled, this,
                [this, id, watched] { m_browser.applyWatched(id, watched); }, Qt::SingleShotConnection);
        }
    });
    connect(&browser, &JellyfinBrowser::logoutRequested, this, [this] {
        close();
        if (!pending()) {
            m_browser.finishLogout();
        } else {
            connect(this, &JellyfinPlaybackAdapter::settled, &m_browser, &JellyfinBrowser::finishLogout,
                    Qt::SingleShotConnection);
        }
    });
}
void JellyfinPlaybackAdapter::play(JellyfinItem const& item, bool fromStart) {
    if (pending()) {
        return;
    }
    quint64 const resolution = ++m_resolution;
    m_browser.setSyncPending(true);
    m_browser.client().resolve(
        item, fromStart,
        [this, resolution](JellyfinPlayback playback, QString error) {
            if (resolution != m_resolution) {
                return;
            }
            m_browser.setSyncPending(false);
            if (!error.isEmpty()) {
                m_browser.showMessage(error);
                return;
            }
            m_player.openMedia(playback.input, playback.startingPositionMilliseconds);
            m_playback = std::move(playback);
            m_mediaId = m_player.mediaItemId();
            m_started = false;
            m_finished = false;
            emit mediaOpened();
            observe();
        },
        this);
}
void JellyfinPlaybackAdapter::observe() {
    if (!m_playback || m_player.mediaItemId() != m_mediaId) {
        return;
    }
    if (m_player.state() == MediaSession::State::Error) {
        finish(m_player.positionMilliseconds());
        return;
    }
    if (m_player.ended() || m_player.stopped()) {
        finish(m_player.positionMilliseconds());
        return;
    }
    if (m_player.state() != MediaSession::State::Ready || m_player.seeking() || !m_player.playRequested() ||
        m_startPending || m_stop) {
        return;
    }
    if (m_started && !m_finished) {
        return;
    }
    m_started = true;
    m_finished = false;
    m_startPending = true;
    m_browser.client().report(
        *m_playback, false, m_player.positionMilliseconds(), m_player.seekable(),
        [this](QString error) {
            m_startPending = false;
            if (!error.isEmpty()) {
                m_browser.showMessage(tr("Playback session reporting failed. %1").arg(error));
            }
            if (m_stop) {
                sendStop();
            } else {
                emit settled();
            }
        },
        this);
}
void JellyfinPlaybackAdapter::finish(qlonglong position) {
    if (!m_playback || !m_started || m_finished) {
        return;
    }
    Q_ASSERT(!m_stop);
    m_finished = true;
    m_stop = Stop{*m_playback, position};
    m_browser.setSyncPending(true);
    if (!m_startPending) {
        sendStop();
    }
}
void JellyfinPlaybackAdapter::sendStop() {
    Q_ASSERT(m_stop && !m_startPending);
    m_browser.client().report(
        m_stop->playback, true, m_stop->position, m_player.seekable(),
        [this](QString error) {
            m_stop.reset();
            m_browser.setSyncPending(false);
            if (!error.isEmpty()) {
                m_browser.showMessage(tr("Playback progress was not saved. %1").arg(error));
            } else {
                m_browser.refresh();
            }
            observe();
            emit settled();
        },
        this);
}
void JellyfinPlaybackAdapter::close() {
    ++m_resolution;
    if (!pending()) {
        m_browser.setSyncPending(false);
    }
    if (m_playback && m_player.mediaItemId() == m_mediaId) {
        m_player.cancel();
    }
}
