#include "platform/PlaybackPowerInhibitor.h"

#include <memory>

#include <QCoreApplication>

#include <qt_windows.h>

#include "diagnostics/LogCategories.h"

namespace {
class WindowsPlaybackPowerInhibitor final : public PlaybackPowerInhibitor {
  public:
    ~WindowsPlaybackPowerInhibitor() override { release(); }

  protected:
    void setActive(bool active) override {
        if (active) {
            acquire();
        } else {
            release();
        }
    }

  private:
    void acquire() {
        Q_ASSERT(m_request == INVALID_HANDLE_VALUE);

        QString const reason = QCoreApplication::translate("PlaybackPowerInhibitor", "Playing media");
        REASON_CONTEXT context{};
        context.Version = POWER_REQUEST_CONTEXT_VERSION;
        context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        context.Reason.SimpleReasonString = const_cast<PWSTR>(reinterpret_cast<PCWSTR>(reason.utf16()));
        HANDLE const request = PowerCreateRequest(&context);
        if (request == INVALID_HANDLE_VALUE) {
            qCWarning(sunplayerLogPlatform, "Could not create the Windows playback power request: %lu", GetLastError());
            return;
        }

        if (!PowerSetRequest(request, PowerRequestDisplayRequired)) {
            qCWarning(sunplayerLogPlatform, "Could not inhibit Windows display idle: %lu", GetLastError());
            CloseHandle(request);
            return;
        }
        if (!PowerSetRequest(request, PowerRequestSystemRequired)) {
            qCWarning(sunplayerLogPlatform, "Could not inhibit Windows system sleep: %lu", GetLastError());
            PowerClearRequest(request, PowerRequestDisplayRequired);
            CloseHandle(request);
            return;
        }

        m_request = request;
        qCDebug(sunplayerLogPlatform, "Acquired the Windows playback power request");
    }

    void release() {
        if (m_request == INVALID_HANDLE_VALUE) {
            return;
        }

        if (!PowerClearRequest(m_request, PowerRequestSystemRequired)) {
            qCWarning(sunplayerLogPlatform, "Could not clear the Windows system-sleep request: %lu", GetLastError());
        }
        if (!PowerClearRequest(m_request, PowerRequestDisplayRequired)) {
            qCWarning(sunplayerLogPlatform, "Could not clear the Windows display-idle request: %lu", GetLastError());
        }
        CloseHandle(m_request);
        m_request = INVALID_HANDLE_VALUE;
        qCDebug(sunplayerLogPlatform, "Released the Windows playback power request");
    }

    HANDLE m_request = INVALID_HANDLE_VALUE;
};
} // namespace

std::unique_ptr<PlaybackPowerInhibitor> createPlaybackPowerInhibitor() {
    return std::make_unique<WindowsPlaybackPowerInhibitor>();
}
