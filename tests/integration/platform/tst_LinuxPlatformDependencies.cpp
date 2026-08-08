#include <QtTest>

#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include <va/va.h>
#include <va/va_drm.h>
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xf86drm.h>

class LinuxPlatformDependenciesTest final : public QObject {
    Q_OBJECT

  private slots:
    void publicApisLinkWithoutOpeningNativeResources();
};

void LinuxPlatformDependenciesTest::publicApisLinkWithoutOpeningNativeResources() {
    auto volatile vulkanLoaderFunction = &vkGetInstanceProcAddr;
    auto volatile waylandConnectFunction = &wl_display_connect;
    auto volatile vaDisplayFunction = &vaGetDisplayDRM;
    auto volatile drmDevicesFunction = &drmGetDevices2;

    QVERIFY(vulkanLoaderFunction != nullptr);
    QVERIFY(waylandConnectFunction != nullptr);
    QVERIFY(vaDisplayFunction != nullptr);
    QVERIFY(drmDevicesFunction != nullptr);
}

QTEST_APPLESS_MAIN(LinuxPlatformDependenciesTest)
#include "tst_LinuxPlatformDependencies.moc"
