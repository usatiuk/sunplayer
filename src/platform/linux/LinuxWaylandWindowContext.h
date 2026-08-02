#pragma once

#include <QVulkanInstance>

#include "platform/linux/WaylandColorManagement.h"

class QGuiApplication;
class QWindow;

// Selects the process Wayland boundary before QGuiApplication is constructed.
void prepareLinuxWaylandPlatform();

// Owns native state whose lifetime must exceed the QWindow surface and every
// recoverable logical-device generation created for that surface.
class LinuxWaylandWindowContext final {
public:
    explicit LinuxWaylandWindowContext(QGuiApplication &application);
    ~LinuxWaylandWindowContext();

    LinuxWaylandWindowContext(const LinuxWaylandWindowContext &) = delete;
    LinuxWaylandWindowContext &operator=(
        const LinuxWaylandWindowContext &) = delete;

    void configureWindow(QWindow &window);
    void releaseWindow(QWindow &window);

    const WaylandSdrSurfaceSelection &surfaceSelection() const;
    bool requiresClientSideDecorations() const;
    QVulkanInstance &vulkanInstance();

private:
    WaylandSdrSurfaceSelection m_surfaceSelection;
    bool m_requiresClientSideDecorations = false;
    QVulkanInstance m_vulkanInstance;
    QWindow *m_window = nullptr;
};
