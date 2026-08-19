#include "platform/PlaybackPowerInhibitor.h"

#include <memory>

#import <Foundation/Foundation.h>

#include <QCoreApplication>

#include "diagnostics/LogCategories.h"

namespace {
class MacPlaybackPowerInhibitor final : public PlaybackPowerInhibitor {
  public:
    ~MacPlaybackPowerInhibitor() override { release(); }

  protected:
    void setActive(bool active) override {
        if (active) {
            Q_ASSERT(!m_activity);
            QString const reason = QCoreApplication::translate("PlaybackPowerInhibitor", "Playing media");
            NSString* const nativeReason =
                [NSString stringWithCharacters:reinterpret_cast<unichar const*>(reason.utf16())
                                        length:static_cast<NSUInteger>(reason.size())];
            m_activity = [[NSProcessInfo processInfo]
                beginActivityWithOptions:NSActivityUserInitiated | NSActivityIdleDisplaySleepDisabled
                                  reason:nativeReason];
            if (m_activity) {
                qCDebug(sunplayerLogPlatform, "Acquired the macOS playback power activity");
            } else {
                qCWarning(sunplayerLogPlatform, "Could not acquire the macOS playback power activity");
            }
        } else {
            release();
        }
    }

  private:
    void release() {
        if (!m_activity) {
            return;
        }
        [[NSProcessInfo processInfo] endActivity:m_activity];
        m_activity = nil;
        qCDebug(sunplayerLogPlatform, "Released the macOS playback power activity");
    }

    id<NSObject> m_activity = nil;
};
} // namespace

std::unique_ptr<PlaybackPowerInhibitor> createPlaybackPowerInhibitor() {
    return std::make_unique<MacPlaybackPowerInhibitor>();
}
