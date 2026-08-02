#include "platform/DisplayStateProvider.h"

#include <cmath>

#include <QWindow>

#include "diagnostics/LogCategories.h"

#ifdef Q_OS_MACOS
#include "platform/macos/MacDisplayStateProvider.h"
#endif

#ifdef Q_OS_WIN

#include <Windows.h>
#include <DispatcherQueue.h>
#include <roapi.h>
#include <windows.graphics.display.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.System.h>
#include <winrt/base.h>

using winrt::Windows::Graphics::Display::AdvancedColorKind;
using winrt::Windows::Graphics::Display::DisplayInformation;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueueController;

namespace {
float nonNegativeOrUnknown(float value, const char *name) {
    if (std::isfinite(value) && value >= 0.0f)
        return value;
    qCWarning(
        sunroomLogPlatform,
        "Windows reported invalid %s: %g",
        name,
        value);
    return 0.0f;
}

enum class WindowsRuntimeState {
    Uninitialized,
    Borrowed,
    Owned,
};

class WindowsDisplayStateProvider final : public DisplayStateProvider {
public:
    explicit WindowsDisplayStateProvider(QObject *parent) : DisplayStateProvider(parent) {}

    ~WindowsDisplayStateProvider() override {
        detach();
        if (m_dispatcherController) {
            try {
                m_dispatcherController.ShutdownQueueAsync();
            } catch (...) {
            }
        }
        if (m_runtimeState == WindowsRuntimeState::Owned)
            RoUninitialize();
    }

    void attach(QWindow &window) override {
        detach();
        if (!ensureWindowsRuntime() || !ensureDispatcherQueue()) {
            publishInvalidState();
            return;
        }

        try {
            const auto factory = winrt::get_activation_factory<
                DisplayInformation,
                IDisplayInformationStaticsInterop>();

            DisplayInformation displayInformation{nullptr};
            winrt::check_hresult(factory->GetForWindow(
                reinterpret_cast<HWND>(window.winId()),
                winrt::guid_of<DisplayInformation>(),
                winrt::put_abi(displayInformation)));

            m_displayInformation = std::move(displayInformation);
            m_changeToken = m_displayInformation.AdvancedColorInfoChanged(
                [this](const DisplayInformation &,
                       const winrt::Windows::Foundation::IInspectable &) {
                    publishCurrentState();
                });
            m_hasChangeToken = true;
            publishCurrentState();
        } catch (const winrt::hresult_error &error) {
            qCWarning(
                sunroomLogPlatform,
                "Windows HDR display monitoring is unavailable: "
                "0x%08X %ls",
                static_cast<unsigned int>(error.code().value),
                error.message().c_str());
            detach();
            publishInvalidState();
        }
    }

    void detach() override {
        if (m_displayInformation && m_hasChangeToken) {
            try {
                m_displayInformation.AdvancedColorInfoChanged(m_changeToken);
            } catch (...) {
            }
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
        if (m_runtimeState != WindowsRuntimeState::Uninitialized)
            return true;

        const HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
        if (SUCCEEDED(result)) {
            m_runtimeState = WindowsRuntimeState::Owned;
            return true;
        }
        if (result == RPC_E_CHANGED_MODE) {
            m_runtimeState = WindowsRuntimeState::Borrowed;
            return true;
        }

        qCWarning(
            sunroomLogPlatform,
            "Could not initialize the Windows Runtime: 0x%08X",
            static_cast<unsigned int>(result));
        return false;
    }

    bool ensureDispatcherQueue() {
        try {
            if (DispatcherQueue::GetForCurrentThread())
                return true;
        } catch (const winrt::hresult_error &error) {
            qCWarning(
                sunroomLogPlatform,
                "Could not query the Windows DispatcherQueue: "
                "0x%08X %ls",
                static_cast<unsigned int>(error.code().value),
                error.message().c_str());
            return false;
        }

        DispatcherQueueOptions options{
            sizeof(DispatcherQueueOptions),
            DQTYPE_THREAD_CURRENT,
            DQTAT_COM_NONE,
        };
        ABI::Windows::System::IDispatcherQueueController *controller = nullptr;
        const HRESULT result = CreateDispatcherQueueController(options, &controller);
        if (FAILED(result)) {
            qCWarning(
                sunroomLogPlatform,
                "Could not create a Windows DispatcherQueue: 0x%08X",
                static_cast<unsigned int>(result));
            return false;
        }
        m_dispatcherController = {controller, winrt::take_ownership_from_abi};
        return true;
    }

    void publishCurrentState() {
        Q_ASSERT(m_displayInformation);

        try {
            const auto colorInfo = m_displayInformation.GetAdvancedColorInfo();
            DisplayState state;
            state.valid = true;
            state.hdrActive =
                colorInfo.CurrentAdvancedColorKind() == AdvancedColorKind::HighDynamicRange;
            state.sdrWhiteNits = nonNegativeOrUnknown(
                colorInfo.SdrWhiteLevelInNits(), "SDR white level");
            state.minLuminanceNits = nonNegativeOrUnknown(
                colorInfo.MinLuminanceInNits(), "minimum luminance");
            state.maxLuminanceNits = nonNegativeOrUnknown(
                colorInfo.MaxLuminanceInNits(), "maximum luminance");
            emit stateChanged(state);
        } catch (const winrt::hresult_error &error) {
            qCWarning(
                sunroomLogPlatform,
                "Could not refresh Windows HDR display information: "
                "0x%08X %ls",
                static_cast<unsigned int>(error.code().value),
                error.message().c_str());
            publishInvalidState();
        }
    }

    void publishInvalidState() {
        emit stateChanged(DisplayState{});
    }

    DisplayInformation m_displayInformation{nullptr};
    DispatcherQueueController m_dispatcherController{nullptr};
    winrt::event_token m_changeToken{};
    bool m_hasChangeToken = false;
    WindowsRuntimeState m_runtimeState = WindowsRuntimeState::Uninitialized;
};
}

#else

namespace {
class NullDisplayStateProvider final : public DisplayStateProvider {
public:
    explicit NullDisplayStateProvider(QObject *parent) : DisplayStateProvider(parent) {}
    void attach(QWindow &) override { emit stateChanged(DisplayState{}); }
    void detach() override {}
    void refresh() override {}
};
}

#endif

std::unique_ptr<DisplayStateProvider> createDisplayStateProvider(QObject *parent) {
    qRegisterMetaType<DisplayState>();
#ifdef Q_OS_WIN
    return std::make_unique<WindowsDisplayStateProvider>(parent);
#elif defined(Q_OS_MACOS)
    return createMacDisplayStateProvider(parent);
#else
    return std::make_unique<NullDisplayStateProvider>(parent);
#endif
}
