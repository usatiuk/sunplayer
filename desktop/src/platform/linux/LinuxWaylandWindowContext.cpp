#include "platform/linux/LinuxWaylandWindowContext.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <QColorSpace>
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QVersionNumber>
#include <QWindow>
#include <qguiapplication_platform.h>
#include <rhi/qrhi_platform.h>

#include <wayland-client.h>

#include "diagnostics/LogCategories.h"
#include "qwayland-color-management-v1.h"

namespace {
class ColorManagerProbe final : public QtWayland::wp_color_manager_v1 {
public:
    ColorManagerProbe(
            wl_registry *registry,
            std::uint32_t name,
            std::uint32_t version,
            WaylandColorManagementCapabilities &capabilities)
        : QtWayland::wp_color_manager_v1(
              registry,
              name,
              static_cast<int>(std::min(
                  version,
                  static_cast<std::uint32_t>(
                      wp_color_manager_v1_interface.version)))),
          m_capabilities(capabilities) {
        m_capabilities.protocolAdvertised = true;
    }

    ~ColorManagerProbe() override {
        if (isInitialized())
            destroy();
    }

protected:
    void wp_color_manager_v1_supported_intent(
            std::uint32_t renderIntent) override {
        if (renderIntent == render_intent_perceptual)
            m_capabilities.perceptualIntent = true;
    }

    void wp_color_manager_v1_supported_feature(
            std::uint32_t feature) override {
        if (feature == feature_parametric)
            m_capabilities.parametricDescriptions = true;
    }

    void wp_color_manager_v1_supported_tf_named(
            std::uint32_t transferFunction) override {
        if (transferFunction == transfer_function_gamma22)
            m_capabilities.gamma22Transfer = true;
        if (transferFunction == transfer_function_ext_linear)
            m_capabilities.extendedLinearTransfer = true;
    }

    void wp_color_manager_v1_supported_primaries_named(
            std::uint32_t primaries) override {
        if (primaries == primaries_srgb)
            m_capabilities.namedSrgbPrimaries = true;
    }

    void wp_color_manager_v1_done() override {
        m_capabilities.inventoryComplete = true;
    }

private:
    WaylandColorManagementCapabilities &m_capabilities;
};

struct RegistryProbe {
    WaylandColorManagementCapabilities capabilities;
    bool decorationManagerAdvertised = false;
    std::unique_ptr<ColorManagerProbe> colorManager;
};

struct WaylandCapabilityInventory {
    WaylandColorManagementCapabilities colorManagement;
    bool decorationManagerAdvertised = false;
};

void handleGlobal(
        void *data,
        wl_registry *registry,
        std::uint32_t name,
        const char *interface,
        std::uint32_t version) {
    auto &probe = *static_cast<RegistryProbe *>(data);
    if (std::strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
        probe.decorationManagerAdvertised = true;
        return;
    }
    if (std::strcmp(interface, wp_color_manager_v1_interface.name) != 0
            || probe.colorManager) {
        return;
    }
    probe.colorManager = std::make_unique<ColorManagerProbe>(
        registry, name, version, probe.capabilities);
}

void handleGlobalRemove(void *, wl_registry *, std::uint32_t) {
}

constexpr wl_registry_listener registryListener{
    .global = handleGlobal,
    .global_remove = handleGlobalRemove,
};

void roundtripOrFail(wl_display &display, const char *operation) {
    if (wl_display_roundtrip(&display) >= 0)
        return;
    qCFatal(
        sunroomLogPlatform,
        "Wayland connection failed while %s (error %d)",
        operation,
        wl_display_get_error(&display));
}

WaylandCapabilityInventory inventoryWaylandCapabilities(
        QGuiApplication &application) {
    auto *const native = application.nativeInterface<
        QNativeInterface::QWaylandApplication>();
    if (!native || !native->display()) {
        qCFatal(
            sunroomLogPlatform,
            "The Wayland QPA did not expose its wl_display");
    }

    wl_display &display = *native->display();
    wl_registry *const registry = wl_display_get_registry(&display);
    if (!registry) {
        qCFatal(
            sunroomLogPlatform,
            "Could not create the Wayland capability registry");
    }

    RegistryProbe probe;
    if (wl_registry_add_listener(registry, &registryListener, &probe) < 0) {
        wl_registry_destroy(registry);
        qCFatal(
            sunroomLogPlatform,
            "Could not observe Wayland globals");
    }

    // The first roundtrip publishes globals. A manager bound from that
    // callback needs the second roundtrip to deliver its complete inventory.
    roundtripOrFail(display, "discovering color-management-v1");
    if (probe.colorManager)
        roundtripOrFail(display, "reading color-management-v1 capabilities");

    probe.colorManager.reset();
    wl_registry_destroy(registry);
    return {
        .colorManagement = probe.capabilities,
        .decorationManagerAdvertised =
            probe.decorationManagerAdvertised,
    };
}
}

void prepareLinuxWaylandPlatform() {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland"));
}

LinuxWaylandWindowContext::LinuxWaylandWindowContext(
        QGuiApplication &application) {
    if (QGuiApplication::platformName() != QStringLiteral("wayland")) {
        qCFatal(
            sunroomLogPlatform,
            "Sunroom requires native Wayland; Qt selected QPA '%s'",
            qPrintable(QGuiApplication::platformName()));
    }

    const WaylandCapabilityInventory inventory =
        inventoryWaylandCapabilities(application);
    m_requiresClientSideDecorations =
        !inventory.decorationManagerAdvertised;
    m_surfaceSelection = selectWaylandSdrSurface(
        inventory.colorManagement);

    const QVersionNumber supportedApi =
        m_vulkanInstance.supportedApiVersion();
    if (supportedApi < QVersionNumber(1, 3)) {
        qCFatal(
            sunroomLogGraphics,
            "Sunroom requires Vulkan 1.3; the loader reports %s",
            qPrintable(supportedApi.toString()));
    }
    m_vulkanInstance.setApiVersion(QVersionNumber(1, 3));
    m_vulkanInstance.setExtensions(
        QRhiVulkanInitParams::preferredInstanceExtensions());
    if (!m_vulkanInstance.create()) {
        qCFatal(
            sunroomLogGraphics,
            "Could not create the Vulkan 1.3 instance");
    }

    qCInfo(sunroomLogPlatform).noquote()
        << "event=wayland.surface_contract"
        << "mode=" + waylandSdrSurfaceModeName(m_surfaceSelection.mode)
        << "managedHdrObservable=" + QString(
            inventory.colorManagement.supportsManagedHdrObservation()
            ? QStringLiteral("true")
            : QStringLiteral("false"))
        << "windowChrome=" + QString(
            m_requiresClientSideDecorations
            ? QStringLiteral("application")
            : QStringLiteral("server-decoration-assumed"))
        << "detail=" + m_surfaceSelection.diagnostic;
}

LinuxWaylandWindowContext::~LinuxWaylandWindowContext() {
    Q_ASSERT(!m_window);
}

void LinuxWaylandWindowContext::configureWindow(QWindow &window) {
    Q_ASSERT(!m_window);
    Q_ASSERT(!window.handle());
    Q_ASSERT(window.surfaceType() == QSurface::VulkanSurface);

    QSurfaceFormat format = window.requestedFormat();
    if (m_surfaceSelection.mode
            == WaylandSdrSurfaceMode::ManagedGamma22) {
        format.setColorSpace(QColorSpace::SRgb);
    } else {
        format.setColorSpace(QColorSpace{});
    }
    window.setFormat(format);
    window.setVulkanInstance(&m_vulkanInstance);
    window.create();
    if (!window.handle()) {
        qCFatal(
            sunroomLogPlatform,
            "Qt could not create the native Wayland window surface");
    }
    m_window = &window;
}

void LinuxWaylandWindowContext::releaseWindow(QWindow &window) {
    Q_ASSERT(m_window == &window);
    window.destroy();
    window.setVulkanInstance(nullptr);
    m_window = nullptr;
}

const WaylandSdrSurfaceSelection &
LinuxWaylandWindowContext::surfaceSelection() const {
    return m_surfaceSelection;
}

bool LinuxWaylandWindowContext::requiresClientSideDecorations() const {
    return m_requiresClientSideDecorations;
}

QVulkanInstance &LinuxWaylandWindowContext::vulkanInstance() {
    Q_ASSERT(m_vulkanInstance.isValid());
    return m_vulkanInstance;
}
