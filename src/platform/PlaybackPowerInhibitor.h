#pragma once

#include <memory>

class MediaSession;

class PlaybackPowerInhibitor {
  public:
    virtual ~PlaybackPowerInhibitor() = default;

    void reconcile(MediaSession const& session);

  protected:
    virtual void setActive(bool active) = 0;

  private:
    bool m_active = false;
};

std::unique_ptr<PlaybackPowerInhibitor> createPlaybackPowerInhibitor();
