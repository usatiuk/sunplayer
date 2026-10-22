#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

#include <QVulkanInstance>

#include "platform/linux/WaylandColorManagement.h"

class QGuiApplication;
class QObject;
class QWindow;
class DisplayStateProvider;

class LinuxWaylandStartupError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Selects the process Wayland boundary before QGuiApplication is constructed.
void prepareLinuxWaylandPlatform();

// Owns native state whose lifetime must exceed the QWindow surface and every
// recoverable logical-device generation created for that surface.
class LinuxWaylandWindowContext final : public PresentationSurfaceController {
  public:
    explicit LinuxWaylandWindowContext(QGuiApplication& application);
    ~LinuxWaylandWindowContext();

    LinuxWaylandWindowContext(LinuxWaylandWindowContext const&) = delete;
    LinuxWaylandWindowContext& operator=(LinuxWaylandWindowContext const&) = delete;

    void configureWindow(QWindow& window, std::function<void(bool)> tilingChanged = {});
    void releaseWindow(QWindow& window);
    std::unique_ptr<DisplayStateProvider> takeDisplayStateProvider(QObject* parent);

    bool requiresClientSideDecorations() const;
    QVulkanInstance& vulkanInstance();

    PresentationSurfaceContract desiredContract(DisplayState const& display,
                                                std::uint64_t graphicsDeviceGeneration) override;
    Preparation prepareFrame(QWindow& window, PresentationSurfaceContract const& contract, float headroom,
                             ColorPrimaries const& videoPrimaries) override;
    void applyPrepared(QWindow& window) override;
    void rejectTarget(std::uint64_t graphicsDeviceGeneration, PresentationSurfaceContract const& contract,
                      char const* reason) override;

  private:
    struct NativeState;

    std::unique_ptr<NativeState> m_nativeState;
    WaylandColorManagementCapabilities m_colorCapabilities;
    WaylandSurfaceSelection m_surfaceSelection;
    WaylandSurfaceRejection m_surfaceRejection;
    WaylandPresentationMode m_selectedMode = WaylandPresentationMode::UnmanagedSrgb;
    bool m_requiresClientSideDecorations = false;
    QVulkanInstance m_vulkanInstance;
    QWindow* m_window = nullptr;
};
