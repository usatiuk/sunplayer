#pragma once
#include "jellyfin/JellyfinBrowser.h"
#include "playback/MediaSession.h"
#include <QObject>
#include <optional>

class JellyfinPlaybackAdapter final : public QObject {
    Q_OBJECT
  public:
    JellyfinPlaybackAdapter(JellyfinBrowser& browser, MediaSession& player, QObject* parent = nullptr);
    void close();
    bool pending() const { return m_startPending || m_stop.has_value(); }
  signals:
    void mediaOpened();
    void settled();

  private:
    void play(JellyfinItem const& item, bool fromStart);
    void observe();
    void finish(qlonglong position);
    void sendStop();
    JellyfinBrowser& m_browser;
    MediaSession& m_player;
    std::optional<JellyfinPlayback> m_playback;
    struct Stop {
        JellyfinPlayback playback;
        qlonglong position;
    };
    std::optional<Stop> m_stop;
    quint64 m_mediaId = 0;
    quint64 m_resolution = 0;
    bool m_started = false;
    bool m_finished = false;
    bool m_startPending = false;
};
