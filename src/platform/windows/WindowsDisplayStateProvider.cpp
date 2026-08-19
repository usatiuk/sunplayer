#include "platform/windows/WindowsDisplayStateProvider.h"

#include <cmath>
#include <optional>
#include <utility>

#include <QWindow>

#include <DispatcherQueue.h>
#include <Windows.h>
#include <roapi.h>
#include <windows.graphics.display.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.System.h>
#include <winrt/base.h>

#include "diagnostics/LogCategories.h"
#include "platform/DisplayStateProvider.h"

using winrt::Windows::Graphics::Display::AdvancedColorKind;
using winrt::Windows::Graphics::Display::DisplayInformation;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueueController;

namespace {
std::optional<DisplayColorMode> displayColorMode(AdvancedColorKind kind) {
    switch (kind) {
    case AdvancedColorKind::StandardDynamicRange:
        return DisplayColorMode::StandardDynamicRange;
    case AdvancedColorKind::WideColorGamut:
        return DisplayColorMode::WideColorGamut;
    case AdvancedColorKind::HighDynamicRange:
        return DisplayColorMode::HighDynamicRange;
    }
    qCWarning(sunplayerLogPlatform, "Windows reported an unknown Advanced Color kind: %d", static_cast<int>(kind));
    return std::nullopt;
}

ColorChromaticity chromaticity(winrt::Windows::Foundation::Point const& point) { return {point.X, point.Y}; }

float nonNegativeOrUnknown(float value, char const* name) {
    if (std::isfinite(value) && value >= 0.0f) {
        return value;
    }
    qCWarning(sunplayerLogPlatform, "Windows reported invalid %s: %g", name, value);
    return 0.0f;
}

enum class WindowsRuntimeState {
    Uninitialized,
    Borrowed,
    Owned,
};

class WindowsDisplayStateProvider final : public DisplayStateProvider {
  public:
    explicit WindowsDisplayStateProvider(QObject* parent) : DisplayStateProvider(parent) {}

    ~WindowsDisplayStateProvider() override {
        detach();
        if (m_dispatcherController) {
            try {
                m_dispatcherController.ShutdownQueueAsync();
            } catch (...) {}
        }
        if (m_runtimeState == WindowsRuntimeState::Owned) {
            RoUninitialize();
        }
    }

    void attach(QWindow& window) override {
        detach();
        if (!ensureWindowsRuntime() || !ensureDispatcherQueue()) {
            publishInvalidState();
            return;
        }

        try {
            auto const factory = winrt::get_activation_factory<DisplayInformation, IDisplayInformationStaticsInterop>();

            DisplayInformation displayInformation{nullptr};
            winrt::check_hresult(factory->GetForWindow(reinterpret_cast<HWND>(window.winId()),
                                                       winrt::guid_of<DisplayInformation>(),
                                                       winrt::put_abi(displayInformation)));

            m_displayInformation = std::move(displayInformation);
            m_changeToken = m_displayInformation.AdvancedColorInfoChanged(
                [this](DisplayInformation const&, winrt::Windows::Foundation::IInspectable const&) {
                    publishCurrentState();
                });
            m_hasChangeToken = true;
            publishCurrentState();
        } catch (winrt::hresult_error const& error) {
            qCWarning(sunplayerLogPlatform,
                      "Windows Advanced Color display monitoring is unavailable: "
                      "0x%08X %ls",
                      static_cast<unsigned int>(error.code().value), error.message().c_str());
            detach();
            publishInvalidState();
        }
    }

    void detach() override {
        if (m_displayInformation && m_hasChangeToken) {
            try {
                m_displayInformation.AdvancedColorInfoChanged(m_changeToken);
            } catch (...) {}
        }
        m_hasChangeToken = false;
        m_displayInformation = nullptr;
    }

    void refresh() override {
        if (!m_displayInformation) {
            publishInvalidState();
            return;
        }
        publishCurrentState();
    }

  private:
    bool ensureWindowsRuntime() {
        if (m_runtimeState != WindowsRuntimeState::Uninitialized) {
            return true;
        }

        HRESULT const result = RoInitialize(RO_INIT_SINGLETHREADED);
        if (SUCCEEDED(result)) {
            m_runtimeState = WindowsRuntimeState::Owned;
            return true;
        }
        if (result == RPC_E_CHANGED_MODE) {
            m_runtimeState = WindowsRuntimeState::Borrowed;
            return true;
        }

        qCWarning(sunplayerLogPlatform, "Could not initialize the Windows Runtime: 0x%08X",
                  static_cast<unsigned int>(result));
        return false;
    }

    bool ensureDispatcherQueue() {
        try {
            if (DispatcherQueue::GetForCurrentThread()) {
                return true;
            }
        } catch (winrt::hresult_error const& error) {
            qCWarning(sunplayerLogPlatform,
                      "Could not query the Windows DispatcherQueue: "
                      "0x%08X %ls",
                      static_cast<unsigned int>(error.code().value), error.message().c_str());
            return false;
        }

        DispatcherQueueOptions options{
            sizeof(DispatcherQueueOptions),
            DQTYPE_THREAD_CURRENT,
            DQTAT_COM_NONE,
        };
        ABI::Windows::System::IDispatcherQueueController* controller = nullptr;
        HRESULT const result = CreateDispatcherQueueController(options, &controller);
        if (FAILED(result)) {
            qCWarning(sunplayerLogPlatform, "Could not create a Windows DispatcherQueue: 0x%08X",
                      static_cast<unsigned int>(result));
            return false;
        }
        m_dispatcherController = {controller, winrt::take_ownership_from_abi};
        return true;
    }

    void publishCurrentState() {
        Q_ASSERT(m_displayInformation);

        try {
            auto const colorInfo = m_displayInformation.GetAdvancedColorInfo();
            std::optional<DisplayColorMode> const colorMode = displayColorMode(colorInfo.CurrentAdvancedColorKind());
            if (!colorMode) {
                publishInvalidState();
                return;
            }
            DisplayState state;
            state.valid = true;
            state.colorMode = *colorMode;
            if (state.colorMode == DisplayColorMode::WideColorGamut) {
                state.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
            } else if (state.colorMode == DisplayColorMode::HighDynamicRange) {
                state.luminanceBehavior = DisplayLuminanceBehavior::SceneReferred;
            }
            if (state.colorMode != DisplayColorMode::StandardDynamicRange) {
                ColorPrimaries const primaries{
                    .red = chromaticity(colorInfo.RedPrimary()),
                    .green = chromaticity(colorInfo.GreenPrimary()),
                    .blue = chromaticity(colorInfo.BluePrimary()),
                    .white = chromaticity(colorInfo.WhitePoint()),
                };
                if (primaries.isValid()) {
                    state.targetPrimariesKnown = true;
                    state.targetPrimaries = primaries;
                } else {
                    qCWarning(sunplayerLogPlatform,
                              "Windows reported invalid Advanced Color display primaries; using the BT.709 fallback");
                }
            }
            float const rawSdrWhiteNits = colorInfo.SdrWhiteLevelInNits();
            float const rawMinimumNits = colorInfo.MinLuminanceInNits();
            float const rawMaximumNits = colorInfo.MaxLuminanceInNits();
            state.sdrWhiteNits = nonNegativeOrUnknown(rawSdrWhiteNits, "SDR white level");
            state.minLuminanceNits = nonNegativeOrUnknown(rawMinimumNits, "minimum luminance");
            state.maxLuminanceNits = nonNegativeOrUnknown(rawMaximumNits, "maximum luminance");
            if (state.colorMode == DisplayColorMode::HighDynamicRange) {
                state.sdrWhiteKnown = std::isfinite(rawSdrWhiteNits) && rawSdrWhiteNits > 0.0f;
                state.luminanceKnown = isValidDisplayLuminanceRange(rawMinimumNits, rawMaximumNits);
                if (std::isfinite(rawMinimumNits) && rawMinimumNits >= 0.0f &&
                    std::isfinite(rawMaximumNits) && rawMaximumNits >= 0.0f && rawMinimumNits > rawMaximumNits) {
                    qCWarning(sunplayerLogPlatform,
                              "Windows reported minimum luminance above maximum luminance; treating the range as "
                              "unknown");
                }
            } else if (state.colorMode == DisplayColorMode::WideColorGamut) {
                state.currentHeadroom = 1.0f;
                state.potentialHeadroom = 1.0f;
            }
            emit stateChanged(state);
        } catch (winrt::hresult_error const& error) {
            qCWarning(sunplayerLogPlatform,
                      "Could not refresh Windows Advanced Color display information: "
                      "0x%08X %ls",
                      static_cast<unsigned int>(error.code().value), error.message().c_str());
            publishInvalidState();
        }
    }

    void publishInvalidState() { emit stateChanged(DisplayState{}); }

    DisplayInformation m_displayInformation{nullptr};
    DispatcherQueueController m_dispatcherController{nullptr};
    winrt::event_token m_changeToken{};
    bool m_hasChangeToken = false;
    WindowsRuntimeState m_runtimeState = WindowsRuntimeState::Uninitialized;
};
} // namespace

std::unique_ptr<DisplayStateProvider> createWindowsDisplayStateProvider(QObject* parent) {
    return std::make_unique<WindowsDisplayStateProvider>(parent);
}
