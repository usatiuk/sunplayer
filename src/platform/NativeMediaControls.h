#pragma once

#include <memory>

class MediaSession;
class QWindow;

class NativeMediaControls {
  public:
    virtual ~NativeMediaControls() = default;
};

// The application owns the integration before destroying either dependency.
std::unique_ptr<NativeMediaControls> createNativeMediaControls(MediaSession& session, QWindow& window);
