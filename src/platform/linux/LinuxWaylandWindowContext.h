#pragma once

#include <memory>
#include <optional>

#include <QVulkanInstance>

#include "platform/linux/WaylandColorManagement.h"

class QGuiApplication;
class QObject;
class QWindow;
class DisplayStateProvider;

// Selects the process Wayland boundary before QGuiApplication is constructed.
void prepareLinuxWaylandPlatform();

// Owns native state whose lifetime must exceed the QWindow surface and every
// recoverable logical-device generation created for that surface.
class LinuxWaylandWindowContext final
    : public PresentationSurfaceController {
public:
    explicit LinuxWaylandWindowContext(QGuiApplication &application);
    ~LinuxWaylandWindowContext();

    LinuxWaylandWindowContext(const LinuxWaylandWindowContext &) = delete;
    LinuxWaylandWindowContext &operator=(
        const LinuxWaylandWindowContext &) = delete;

    void configureWindow(QWindow &window);
    void releaseWindow(QWindow &window);
    std::unique_ptr<DisplayStateProvider> takeDisplayStateProvider(
        QObject *parent);

    const WaylandSurfaceSelection &surfaceSelection() const;
    bool requiresClientSideDecorations() const;
    QVulkanInstance &vulkanInstance();

    PresentationSurfaceMode desiredMode(
        std::uint64_t graphicsDeviceGeneration) override;
    void applyMode(
        QWindow &window,
        PresentationSurfaceMode mode) override;
    void rejectHdrTarget(
        std::uint64_t graphicsDeviceGeneration,
        const char *reason) override;

private:
    struct NativeState;

    std::unique_ptr<NativeState> m_nativeState;
    WaylandColorManagementCapabilities m_colorCapabilities;
    WaylandSurfaceSelection m_surfaceSelection;
    std::optional<WaylandHdrRejection> m_hdrRejection;
    bool m_requiresClientSideDecorations = false;
    QVulkanInstance m_vulkanInstance;
    QWindow *m_window = nullptr;
};
