#include "platform/linux/LinuxWaylandWindowContext.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <QColorSpace>
#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QSurfaceFormat>
#include <QVersionNumber>
#include <QWindow>
#include <qguiapplication_platform.h>
#include <qpa/qplatformwindow_p.h>
#include <rhi/qrhi_platform.h>

#include <unistd.h>
#include <wayland-client.h>

#include "diagnostics/LogCategories.h"
#include "platform/DisplayStateProvider.h"
#include "qwayland-color-management-v1.h"
#include "wayland-color-management-v1-client-protocol.h"

namespace {
static_assert(WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_PREFERRED_CHANGED2_SINCE_VERSION ==
              WaylandColorManagementCapabilities::requiredProtocolVersion);
static_assert(WP_IMAGE_DESCRIPTION_V1_READY2_SINCE_VERSION ==
              WaylandColorManagementCapabilities::requiredProtocolVersion);

std::uint64_t imageDescriptionIdentity(std::uint32_t high, std::uint32_t low) {
    return (static_cast<std::uint64_t>(high) << 32U) | low;
}

QString presentationModeName(PresentationSurfaceMode mode) {
    switch (mode) {
    case PresentationSurfaceMode::AdaptiveExtendedLinear:
        return QStringLiteral("adaptive-extended-linear");
    case PresentationSurfaceMode::UnmanagedSrgb:
        return QStringLiteral("unmanaged-srgb");
    case PresentationSurfaceMode::ManagedGamma22Sdr:
        return QStringLiteral("managed-gamma22-sdr");
    case PresentationSurfaceMode::ManagedHdr10Pq:
        return QStringLiteral("managed-hdr10-pq");
    }
    Q_UNREACHABLE_RETURN(QString{});
}

WaylandColorPrimaries colorPrimaries(std::int32_t redX, std::int32_t redY, std::int32_t greenX, std::int32_t greenY,
                                     std::int32_t blueX, std::int32_t blueY, std::int32_t whiteX, std::int32_t whiteY) {
    constexpr float protocolScale = 1'000'000.0f;
    return {
        .red =
            {
                static_cast<float>(redX) / protocolScale,
                static_cast<float>(redY) / protocolScale,
            },
        .green =
            {
                static_cast<float>(greenX) / protocolScale,
                static_cast<float>(greenY) / protocolScale,
            },
        .blue =
            {
                static_cast<float>(blueX) / protocolScale,
                static_cast<float>(blueY) / protocolScale,
            },
        .white =
            {
                static_cast<float>(whiteX) / protocolScale,
                static_cast<float>(whiteY) / protocolScale,
            },
    };
}

class ColorManagerBinding final : public QtWayland::wp_color_manager_v1 {
  public:
    ColorManagerBinding(wl_registry* registry, std::uint32_t name, std::uint32_t advertisedVersion)
        : QtWayland::wp_color_manager_v1(
              registry, name, static_cast<int>(WaylandColorManagementCapabilities::requiredProtocolVersion)) {
        Q_ASSERT(advertisedVersion >= WaylandColorManagementCapabilities::requiredProtocolVersion);
        m_capabilities.protocolAdvertised = true;
        m_capabilities.protocolVersion = advertisedVersion;
    }

    ~ColorManagerBinding() override {
        if (isInitialized()) {
            destroy();
        }
    }

    WaylandColorManagementCapabilities const& capabilities() const { return m_capabilities; }

  protected:
    void wp_color_manager_v1_supported_intent(std::uint32_t renderIntent) override {
        if (renderIntent == render_intent_perceptual) {
            m_capabilities.perceptualIntent = true;
        }
    }

    void wp_color_manager_v1_supported_feature(std::uint32_t feature) override {
        if (feature == feature_parametric) {
            m_capabilities.parametricDescriptions = true;
        }
    }

    void wp_color_manager_v1_supported_tf_named(std::uint32_t transferFunction) override {
        if (transferFunction == transfer_function_gamma22) {
            m_capabilities.gamma22Transfer = true;
        }
        if (transferFunction == transfer_function_st2084_pq) {
            m_capabilities.pqTransfer = true;
        }
    }

    void wp_color_manager_v1_supported_primaries_named(std::uint32_t primaries) override {
        if (primaries == primaries_srgb) {
            m_capabilities.namedSrgbPrimaries = true;
        }
        if (primaries == primaries_bt2020) {
            m_capabilities.namedBt2020Primaries = true;
        }
    }

    void wp_color_manager_v1_done() override { m_capabilities.inventoryComplete = true; }

  private:
    WaylandColorManagementCapabilities m_capabilities;
};

class ManagedImageDescription final : public QtWayland::wp_image_description_v1 {
  public:
    enum class Result {
        Pending,
        Ready,
        Failed,
    };

    explicit ManagedImageDescription(struct ::wp_image_description_v1* description)
        : QtWayland::wp_image_description_v1(description) {}

    ~ManagedImageDescription() override {
        if (isInitialized()) {
            destroy();
        }
    }

    bool complete() const { return m_result != Result::Pending; }
    bool ready() const { return m_result == Result::Ready; }

  protected:
    void wp_image_description_v1_failed(std::uint32_t cause, QString const& message) override {
        Q_ASSERT(m_result == Result::Pending);
        m_result = Result::Failed;
        qCWarning(sunplayerLogPlatform).noquote() << "event=wayland.surface_description_failed"
                                                << "cause=" + QString::number(cause) << "detail=" + message;
    }

    void wp_image_description_v1_ready2(std::uint32_t, std::uint32_t) override {
        Q_ASSERT(m_result == Result::Pending);
        m_result = Result::Ready;
    }

  private:
    Result m_result = Result::Pending;
};

std::unique_ptr<ManagedImageDescription>
createManagedDescription(ColorManagerBinding& colorManager, std::uint32_t primaries, std::uint32_t transferFunction) {
    auto* const creator = colorManager.create_parametric_creator();
    Q_ASSERT(creator);
    wp_image_description_creator_params_v1_set_primaries_named(creator, primaries);
    wp_image_description_creator_params_v1_set_tf_named(creator, transferFunction);
    return std::make_unique<ManagedImageDescription>(wp_image_description_creator_params_v1_create(creator));
}

class ManagedSurface final : public QtWayland::wp_color_management_surface_v1 {
  public:
    explicit ManagedSurface(struct ::wp_color_management_surface_v1* surface)
        : QtWayland::wp_color_management_surface_v1(surface) {}

    ~ManagedSurface() override {
        if (isInitialized()) {
            destroy();
        }
    }

    void apply(ManagedImageDescription& description) {
        Q_ASSERT(description.ready());
        set_image_description(description.object(), QtWayland::wp_color_manager_v1::render_intent_perceptual);
    }
};

struct RegistryInventory {
    std::uint32_t colorManagerName = 0;
    std::uint32_t colorManagerVersion = 0;
    bool decorationManagerAdvertised = false;
};

void handleGlobal(void* data, wl_registry*, std::uint32_t name, char const* interface, std::uint32_t version) {
    auto& inventory = *static_cast<RegistryInventory*>(data);
    if (std::strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
        inventory.decorationManagerAdvertised = true;
        return;
    }
    if (std::strcmp(interface, wp_color_manager_v1_interface.name) == 0 && inventory.colorManagerName == 0) {
        inventory.colorManagerName = name;
        inventory.colorManagerVersion = version;
    }
}

void handleGlobalRemove(void*, wl_registry*, std::uint32_t) {}

constexpr wl_registry_listener registryListener{
    .global = handleGlobal,
    .global_remove = handleGlobalRemove,
};

void roundtripOrFail(wl_display& display, char const* operation) {
    if (wl_display_roundtrip(&display) >= 0) {
        return;
    }
    qCFatal(sunplayerLogPlatform, "Wayland connection failed while %s (error %d)", operation,
            wl_display_get_error(&display));
}

void waitForManagedDescriptions(wl_display& display, ManagedImageDescription& sdrDescription,
                                ManagedImageDescription* hdrDescription) {
    // Creation completion is explicitly eventual. The first roundtrip flushes
    // both requests; later dispatches wait for each terminal ready2/failed
    // event instead of mistaking an in-flight description for rejection.
    roundtripOrFail(display, "creating managed surface descriptions");
    while (!sdrDescription.complete() || (hdrDescription && !hdrDescription->complete())) {
        if (wl_display_dispatch(&display) >= 0) {
            continue;
        }
        qCFatal(sunplayerLogPlatform,
                "Wayland connection failed while waiting for managed surface "
                "descriptions (error %d)",
                wl_display_get_error(&display));
    }
}

class PreferredDescriptionInfo final : public QtWayland::wp_image_description_info_v1 {
  public:
    using Completion = std::function<void(WaylandPreferredDescription const&)>;

    PreferredDescriptionInfo(struct ::wp_image_description_info_v1* information, Completion completion)
        : QtWayland::wp_image_description_info_v1(information), m_completion(std::move(completion)) {}

    ~PreferredDescriptionInfo() override {
        if (isInitialized()) {
            wp_image_description_info_v1_destroy(object());
        }
    }

  protected:
    void wp_image_description_info_v1_done() override {
        m_description.parametric = true;
        if (!m_description.targetPrimariesKnown && m_description.primariesKnown) {
            m_description.targetPrimaries = m_description.primaries;
        }
        m_completion(m_description);
    }

    void wp_image_description_info_v1_icc_file(std::int32_t file, std::uint32_t) override {
        // This cannot occur for get_preferred_parametric(), but an unexpected
        // descriptor still belongs to the client and must be closed.
        close(file);
    }

    void wp_image_description_info_v1_primaries(std::int32_t redX, std::int32_t redY, std::int32_t greenX,
                                                std::int32_t greenY, std::int32_t blueX, std::int32_t blueY,
                                                std::int32_t whiteX, std::int32_t whiteY) override {
        m_description.primaries = colorPrimaries(redX, redY, greenX, greenY, blueX, blueY, whiteX, whiteY);
        m_description.primariesKnown = true;
    }

    void wp_image_description_info_v1_tf_power(std::uint32_t) override {
        m_description.transferFunction = WaylandTransferFunction::Other;
    }

    void wp_image_description_info_v1_tf_named(std::uint32_t transferFunction) override {
        switch (transferFunction) {
        case QtWayland::wp_color_manager_v1::transfer_function_gamma22:
            m_description.transferFunction = WaylandTransferFunction::Gamma22;
            break;
        case QtWayland::wp_color_manager_v1::transfer_function_ext_linear:
            m_description.transferFunction = WaylandTransferFunction::ExtendedLinear;
            break;
        case QtWayland::wp_color_manager_v1::transfer_function_st2084_pq:
            m_description.transferFunction = WaylandTransferFunction::Pq;
            break;
        default:
            m_description.transferFunction = WaylandTransferFunction::Other;
            break;
        }
    }

    void wp_image_description_info_v1_luminances(std::uint32_t minimumLuminance, std::uint32_t maximumLuminance,
                                                 std::uint32_t referenceLuminance) override {
        m_description.minimumLuminanceNits = static_cast<float>(minimumLuminance) / 10'000.0f;
        m_description.maximumLuminanceNits = static_cast<float>(maximumLuminance);
        m_description.referenceWhiteNits = static_cast<float>(referenceLuminance);
        m_description.luminancesKnown = true;
    }

    void wp_image_description_info_v1_target_primaries(std::int32_t redX, std::int32_t redY, std::int32_t greenX,
                                                       std::int32_t greenY, std::int32_t blueX, std::int32_t blueY,
                                                       std::int32_t whiteX, std::int32_t whiteY) override {
        m_description.targetPrimaries = colorPrimaries(redX, redY, greenX, greenY, blueX, blueY, whiteX, whiteY);
        m_description.targetPrimariesKnown = true;
    }

    void wp_image_description_info_v1_target_luminance(std::uint32_t minimumLuminance,
                                                       std::uint32_t maximumLuminance) override {
        m_description.targetMinimumLuminanceNits = static_cast<float>(minimumLuminance) / 10'000.0f;
        m_description.targetMaximumLuminanceNits = static_cast<float>(maximumLuminance);
        m_description.targetLuminanceKnown = true;
    }

  private:
    Completion m_completion;
    WaylandPreferredDescription m_description;
};

class PreferredDescriptionRequest final : public QtWayland::wp_image_description_v1 {
  public:
    using Completion = std::function<void(std::uint64_t, std::optional<WaylandPreferredDescription>)>;

    PreferredDescriptionRequest(struct ::wp_image_description_v1* description, Completion completion)
        : QtWayland::wp_image_description_v1(description), m_completion(std::move(completion)) {}

    ~PreferredDescriptionRequest() override {
        m_information.reset();
        if (isInitialized()) {
            destroy();
        }
    }

  protected:
    void wp_image_description_v1_failed(std::uint32_t cause, QString const& message) override {
        qCWarning(sunplayerLogPlatform).noquote() << "event=wayland.preferred_description_failed"
                                                << "cause=" + QString::number(cause) << "detail=" + message;
        m_completion(0, std::nullopt);
    }

    void wp_image_description_v1_ready2(std::uint32_t identityHigh, std::uint32_t identityLow) override {
        std::uint64_t const identity = imageDescriptionIdentity(identityHigh, identityLow);
        m_information = std::make_unique<PreferredDescriptionInfo>(
            get_information(),
            [this, identity](WaylandPreferredDescription const& description) { m_completion(identity, description); });
    }

  private:
    Completion m_completion;
    std::unique_ptr<PreferredDescriptionInfo> m_information;
};

class SurfaceFeedback final : public QtWayland::wp_color_management_surface_feedback_v1 {
  public:
    using Change = std::function<void(std::uint64_t)>;

    SurfaceFeedback(struct ::wp_color_management_surface_feedback_v1* feedback, Change change)
        : QtWayland::wp_color_management_surface_feedback_v1(feedback), m_change(std::move(change)) {}

    ~SurfaceFeedback() override {
        if (isInitialized()) {
            destroy();
        }
    }

  protected:
    void wp_color_management_surface_feedback_v1_preferred_changed2(std::uint32_t identityHigh,
                                                                    std::uint32_t identityLow) override {
        m_change(imageDescriptionIdentity(identityHigh, identityLow));
    }

  private:
    Change m_change;
};
} // namespace

struct LinuxWaylandWindowContext::NativeState final {
    class Provider final : public DisplayStateProvider {
      public:
        Provider(NativeState& nativeState, QObject* parent)
            : DisplayStateProvider(parent), m_nativeState(nativeState) {}

        ~Provider() override { detach(); }

        void attach(QWindow& window) override {
            Q_ASSERT(!m_window);
            Q_ASSERT(!m_nativeState.managedSurface);
            m_window = &window;
            m_window->installEventFilter(this);
            if (!m_nativeState.capabilities.supportsManagedSdr()) {
                publish(DisplayState{});
                return;
            }
            attachNativeWindow();
        }

        void detach() override {
            clearSurface();
            clearNativeWindow();
            if (m_window) {
                m_window->removeEventFilter(this);
            }
            m_window = nullptr;
        }

        void refresh() override {
            if (!m_nativeState.capabilities.supportsManagedSdr()) {
                publish(DisplayState{});
                return;
            }
            requestPreferredDescription();
        }

      protected:
        bool eventFilter(QObject* watched, QEvent* event) override {
            if (watched != m_window || event->type() != QEvent::PlatformSurface) {
                return DisplayStateProvider::eventFilter(watched, event);
            }

            auto const* const surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
            if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
                clearSurface();
                clearNativeWindow();
            } else {
                attachNativeWindow();
            }
            return DisplayStateProvider::eventFilter(watched, event);
        }

      private:
        void attachNativeWindow() {
            Q_ASSERT(m_window);
            clearSurface();
            clearNativeWindow();
            m_nativeWindow = m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
            if (!m_nativeWindow) {
                qCFatal(sunplayerLogPlatform, "The Wayland QPA did not expose the native window interface");
            }
            m_surfaceCreatedConnection =
                connect(m_nativeWindow, &QNativeInterface::Private::QWaylandWindow::surfaceCreated, this,
                        [this] { attachSurface(); });
            m_surfaceDestroyedConnection =
                connect(m_nativeWindow, &QNativeInterface::Private::QWaylandWindow::surfaceDestroyed, this,
                        [this] { clearSurface(); });
            attachSurface();
        }

        void clearNativeWindow() {
            disconnect(m_surfaceCreatedConnection);
            disconnect(m_surfaceDestroyedConnection);
            m_surfaceCreatedConnection = {};
            m_surfaceDestroyedConnection = {};
            m_nativeWindow = nullptr;
        }

        void attachSurface() {
            Q_ASSERT(m_nativeWindow);
            clearSurface();
            wl_surface* const surface = m_nativeWindow->surface();
            if (!surface) {
                return;
            }
            m_nativeState.attachManagedSurface(surface);
            m_feedback = std::make_unique<SurfaceFeedback>(
                m_nativeState.colorManager->get_surface_feedback(surface), [this](std::uint64_t identity) {
                    if (m_preferredIdentity && *m_preferredIdentity == identity) {
                        return;
                    }
                    requestPreferredDescription();
                });
            requestPreferredDescription();
        }

        void clearSurface() {
            ++m_requestSerial;
            m_request.reset();
            m_feedback.reset();
            m_preferredIdentity.reset();
            m_nativeState.detachManagedSurface();
        }

        void requestPreferredDescription() {
            if (!m_feedback) {
                return;
            }
            ++m_requestSerial;
            std::uint64_t const requestSerial = m_requestSerial;
            m_request.reset();
            m_request = std::make_unique<PreferredDescriptionRequest>(
                m_feedback->get_preferred_parametric(),
                [this, requestSerial](std::uint64_t identity, std::optional<WaylandPreferredDescription> description) {
                    if (requestSerial != m_requestSerial) {
                        return;
                    }
                    if (!description) {
                        publish(DisplayState{});
                        return;
                    }
                    std::optional<DisplayState> const state = displayStateFromWaylandDescription(*description);
                    if (!state) {
                        qCWarning(sunplayerLogPlatform).noquote() << "event=wayland.preferred_description_invalid";
                        publish(DisplayState{});
                        return;
                    }
                    m_preferredIdentity = identity;
                    publish(*state);
                });
        }

        void publish(DisplayState const& state) {
            if (m_hasPublished && m_published == state) {
                return;
            }
            m_hasPublished = true;
            m_published = state;
            if (state.valid) {
                qCInfo(sunplayerLogPlatform).noquote()
                    << "event=wayland.preferred_description"
                    << "referenceWhiteNits=" + QString::number(state.sdrWhiteNits)
                    << "minimumNits=" + QString::number(state.minLuminanceNits)
                    << "maximumNits=" + QString::number(state.maxLuminanceNits)
                    << "preferredTargetHeadroom=" + QString::number(state.currentHeadroom);
            }
            emit stateChanged(state);
        }

        NativeState& m_nativeState;
        QPointer<QWindow> m_window;
        QPointer<QNativeInterface::Private::QWaylandWindow> m_nativeWindow;
        QMetaObject::Connection m_surfaceCreatedConnection;
        QMetaObject::Connection m_surfaceDestroyedConnection;
        std::unique_ptr<SurfaceFeedback> m_feedback;
        std::unique_ptr<PreferredDescriptionRequest> m_request;
        std::optional<std::uint64_t> m_preferredIdentity;
        std::uint64_t m_requestSerial = 0;
        DisplayState m_published;
        bool m_hasPublished = false;
    };

    explicit NativeState(QGuiApplication& application) {
        auto* const native = application.nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!native || !native->display()) {
            qCFatal(sunplayerLogPlatform, "The Wayland QPA did not expose its wl_display");
        }

        wl_display* const display = native->display();
        wl_registry* const registry = wl_display_get_registry(display);
        if (!registry) {
            qCFatal(sunplayerLogPlatform, "Could not create the Wayland capability registry");
        }

        RegistryInventory inventory;
        if (wl_registry_add_listener(registry, &registryListener, &inventory) < 0) {
            wl_registry_destroy(registry);
            qCFatal(sunplayerLogPlatform, "Could not observe Wayland globals");
        }
        roundtripOrFail(*display, "discovering color-management-v1");

        decorationManagerAdvertised = inventory.decorationManagerAdvertised;
        if (inventory.colorManagerName != 0) {
            capabilities.protocolAdvertised = true;
            capabilities.protocolVersion = inventory.colorManagerVersion;
            if (inventory.colorManagerVersion >= WaylandColorManagementCapabilities::requiredProtocolVersion) {
                colorManager = std::make_unique<ColorManagerBinding>(registry, inventory.colorManagerName,
                                                                     inventory.colorManagerVersion);
                roundtripOrFail(*display, "reading color-management-v1 capabilities");
                capabilities = colorManager->capabilities();
                prepareManagedDescriptions(*display);
            }
        }
        wl_registry_destroy(registry);
    }

    void attachManagedSurface(wl_surface* surface) {
        managedSurface.reset();
        if (!capabilities.supportsManagedSdr()) {
            return;
        }
        managedSurface = std::make_unique<ManagedSurface>(colorManager->get_surface(surface));
        applyDeclaredMode();
    }

    void detachManagedSurface() { managedSurface.reset(); }

    void setDeclaredMode(PresentationSurfaceMode mode) {
        Q_ASSERT(mode == PresentationSurfaceMode::ManagedGamma22Sdr || mode == PresentationSurfaceMode::ManagedHdr10Pq);
        declaredMode = mode;
        if (managedSurface) {
            applyDeclaredMode();
        }
    }

  private:
    void prepareManagedDescriptions(wl_display& display) {
        if (!capabilities.supportsManagedSdr()) {
            return;
        }

        sdrDescription = createManagedDescription(*colorManager, QtWayland::wp_color_manager_v1::primaries_srgb,
                                                  QtWayland::wp_color_manager_v1::transfer_function_gamma22);
        if (capabilities.supportsManagedHdr10()) {
            hdrDescription = createManagedDescription(*colorManager, QtWayland::wp_color_manager_v1::primaries_bt2020,
                                                      QtWayland::wp_color_manager_v1::transfer_function_st2084_pq);
        }
        waitForManagedDescriptions(display, *sdrDescription, hdrDescription.get());

        if (!sdrDescription->ready()) {
            qCWarning(sunplayerLogPlatform, "The compositor rejected the advertised managed-sRGB "
                                          "description; using unmanaged SDR");
            capabilities.parametricDescriptions = false;
            sdrDescription.reset();
            hdrDescription.reset();
            return;
        }
        if (hdrDescription && !hdrDescription->ready()) {
            qCWarning(sunplayerLogPlatform, "The compositor rejected the advertised BT.2020/PQ "
                                          "description; disabling managed HDR");
            capabilities.pqTransfer = false;
            hdrDescription.reset();
        }
    }

    void applyDeclaredMode() {
        Q_ASSERT(managedSurface);
        if (declaredMode == PresentationSurfaceMode::ManagedHdr10Pq) {
            Q_ASSERT(hdrDescription);
            managedSurface->apply(*hdrDescription);
        } else {
            Q_ASSERT(declaredMode == PresentationSurfaceMode::ManagedGamma22Sdr);
            Q_ASSERT(sdrDescription);
            managedSurface->apply(*sdrDescription);
        }
    }

  public:
    WaylandColorManagementCapabilities capabilities;
    bool decorationManagerAdvertised = false;
    std::unique_ptr<ColorManagerBinding> colorManager;
    std::unique_ptr<ManagedImageDescription> sdrDescription;
    std::unique_ptr<ManagedImageDescription> hdrDescription;
    std::unique_ptr<ManagedSurface> managedSurface;
    PresentationSurfaceMode declaredMode = PresentationSurfaceMode::ManagedGamma22Sdr;
    bool displayProviderTaken = false;
};

void prepareLinuxWaylandPlatform() { qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland")); }

LinuxWaylandWindowContext::LinuxWaylandWindowContext(QGuiApplication& application) {
    if (QGuiApplication::platformName() != QStringLiteral("wayland")) {
        qCFatal(sunplayerLogPlatform, "SunPlayer requires native Wayland; Qt selected QPA '%s'",
                qPrintable(QGuiApplication::platformName()));
    }

    m_nativeState = std::make_unique<NativeState>(application);
    m_colorCapabilities = m_nativeState->capabilities;
    m_requiresClientSideDecorations = !m_nativeState->decorationManagerAdvertised;
    m_surfaceSelection = selectWaylandSurface(m_colorCapabilities);
    if (m_surfaceSelection.mode == WaylandSdrSurfaceMode::ManagedGamma22) {
        m_nativeState->setDeclaredMode(m_surfaceSelection.presentationContract().mode);
    }

    QVersionNumber const supportedApi = m_vulkanInstance.supportedApiVersion();
    if (supportedApi < QVersionNumber(1, 3)) {
        qCFatal(sunplayerLogGraphics, "SunPlayer requires Vulkan 1.3; the loader reports %s",
                qPrintable(supportedApi.toString()));
    }
    m_vulkanInstance.setApiVersion(QVersionNumber(1, 3));
    m_vulkanInstance.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
    if (!m_vulkanInstance.create()) {
        qCFatal(sunplayerLogGraphics, "Could not create the Vulkan 1.3 instance");
    }

    qCInfo(sunplayerLogPlatform).noquote()
        << "event=wayland.surface_contract"
        << "protocolVersion=" + QString::number(m_colorCapabilities.protocolVersion)
        << "mode=" + presentationModeName(m_surfaceSelection.presentationContract().mode)
        << "managedHdrDeclarationAvailable=" +
               QString(m_colorCapabilities.supportsManagedHdr10() ? QStringLiteral("true") : QStringLiteral("false"))
        << "windowChrome=" + QString(m_requiresClientSideDecorations ? QStringLiteral("application")
                                                                     : QStringLiteral("server-decoration-assumed"))
        << "detail=" + m_surfaceSelection.diagnostic;
}

LinuxWaylandWindowContext::~LinuxWaylandWindowContext() { Q_ASSERT(!m_window); }

void LinuxWaylandWindowContext::configureWindow(QWindow& window) {
    Q_ASSERT(!m_window);
    Q_ASSERT(!window.handle());
    Q_ASSERT(window.surfaceType() == QSurface::VulkanSurface);

    // SunPlayer owns the one mutable color-management-v1 declaration. QRhi's
    // Wayland Vulkan path uses PASS_THROUGH for the corresponding buffers.
    QSurfaceFormat format = window.requestedFormat();
    format.setColorSpace(QColorSpace{});
    window.setFormat(format);
    window.setVulkanInstance(&m_vulkanInstance);
    window.create();
    if (!window.handle()) {
        qCFatal(sunplayerLogPlatform, "Qt could not create the native Wayland window surface");
    }
    m_window = &window;
}

void LinuxWaylandWindowContext::releaseWindow(QWindow& window) {
    Q_ASSERT(m_window == &window);
    // The one full-window-lifetime provider owns the color-surface follower
    // and must be released after the engine but before the native window.
    Q_ASSERT(!m_nativeState->managedSurface);
    window.destroy();
    window.setVulkanInstance(nullptr);
    m_window = nullptr;
}

std::unique_ptr<DisplayStateProvider> LinuxWaylandWindowContext::takeDisplayStateProvider(QObject* parent) {
    Q_ASSERT(!m_nativeState->displayProviderTaken);
    m_nativeState->displayProviderTaken = true;
    return std::make_unique<NativeState::Provider>(*m_nativeState, parent);
}

WaylandSurfaceSelection const& LinuxWaylandWindowContext::surfaceSelection() const { return m_surfaceSelection; }

bool LinuxWaylandWindowContext::requiresClientSideDecorations() const { return m_requiresClientSideDecorations; }

QVulkanInstance& LinuxWaylandWindowContext::vulkanInstance() {
    Q_ASSERT(m_vulkanInstance.isValid());
    return m_vulkanInstance;
}

PresentationSurfaceMode LinuxWaylandWindowContext::desiredMode(std::uint64_t graphicsDeviceGeneration) {
    return selectWaylandPresentationMode(m_surfaceSelection.mode, m_colorCapabilities, graphicsDeviceGeneration,
                                         m_hdrRejection);
}

void LinuxWaylandWindowContext::applyMode(QWindow& window, PresentationSurfaceMode mode) {
    Q_ASSERT(m_window == &window);
    Q_ASSERT(m_surfaceSelection.mode == WaylandSdrSurfaceMode::ManagedGamma22);
    Q_ASSERT(mode == PresentationSurfaceMode::ManagedGamma22Sdr || mode == PresentationSurfaceMode::ManagedHdr10Pq);
    m_nativeState->setDeclaredMode(mode);

    qCInfo(sunplayerLogPlatform).noquote() << "event=wayland.surface_transition"
                                         << "mode=" + presentationModeName(mode) << "nativeSurface=preserved";
}

void LinuxWaylandWindowContext::rejectHdrTarget(std::uint64_t graphicsDeviceGeneration, char const* reason) {
    Q_ASSERT(graphicsDeviceGeneration != 0);
    m_hdrRejection = WaylandHdrRejection{
        .graphicsDeviceGeneration = graphicsDeviceGeneration,
    };
    qCWarning(sunplayerLogPlatform).noquote()
        << "event=wayland.hdr_surface_rejected"
        << "deviceGeneration=" + QString::number(graphicsDeviceGeneration) << "reason=" + QString::fromUtf8(reason);
}
