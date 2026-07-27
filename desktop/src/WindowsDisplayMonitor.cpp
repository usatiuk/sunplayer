#include "WindowsDisplayMonitor.h"

#include <QWindow>

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

class WindowsDisplayMonitor::Impl {
public:
    explicit Impl(ChangeHandler changeHandler)
        : m_changeHandler(std::move(changeHandler)) {
    }

    ~Impl() {
        detach();

        if (m_dispatcherController) {
            try {
                m_dispatcherController.ShutdownQueueAsync();
            } catch (...) {
                // The process is shutting down; releasing the controller is enough.
            }
        }

        if (m_roInitialized)
            RoUninitialize();
    }

    bool attach(QWindow *window) {
        detach();
        if (!window || !ensureWindowsRuntime() || !ensureDispatcherQueue())
            return false;

        try {
            const auto factory = winrt::get_activation_factory<
                DisplayInformation,
                IDisplayInformationStaticsInterop>();

            DisplayInformation displayInformation{nullptr};
            winrt::check_hresult(factory->GetForWindow(
                reinterpret_cast<HWND>(window->winId()),
                winrt::guid_of<DisplayInformation>(),
                winrt::put_abi(displayInformation)));

            m_displayInformation = std::move(displayInformation);
            m_changeToken = m_displayInformation.AdvancedColorInfoChanged(
                [this](const DisplayInformation &, const winrt::Windows::Foundation::IInspectable &) {
                    publishCurrentState();
                });
            m_hasChangeToken = true;
            publishCurrentState();
            return true;
        } catch (const winrt::hresult_error &error) {
            qWarning("Windows HDR display monitoring is unavailable: 0x%08X %ls",
                     static_cast<unsigned int>(error.code().value),
                     error.message().c_str());
            detach();
            return false;
        }
    }

    void detach() {
        if (m_displayInformation && m_hasChangeToken) {
            try {
                m_displayInformation.AdvancedColorInfoChanged(m_changeToken);
            } catch (...) {
                // The window or display object may already be gone.
            }
        }

        m_hasChangeToken = false;
        m_displayInformation = nullptr;
    }

    void refresh() {
        publishCurrentState();
    }

private:
    bool ensureWindowsRuntime() {
        if (m_roInitialized || m_roAvailable)
            return true;

        const HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
        if (SUCCEEDED(result)) {
            m_roInitialized = true;
            m_roAvailable = true;
            return true;
        }

        // Qt or another library may already have initialized the GUI thread
        // using a different COM apartment. WinRT activation can still work.
        if (result == RPC_E_CHANGED_MODE) {
            m_roAvailable = true;
            return true;
        }

        qWarning("Could not initialize the Windows Runtime: 0x%08X",
                 static_cast<unsigned int>(result));
        return false;
    }

    bool ensureDispatcherQueue() {
        try {
            if (DispatcherQueue::GetForCurrentThread())
                return true;
        } catch (...) {
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
            qWarning("Could not create a Windows DispatcherQueue: 0x%08X",
                     static_cast<unsigned int>(result));
            return false;
        }

        m_dispatcherController = {
            controller,
            winrt::take_ownership_from_abi,
        };
        return true;
    }

    void publishCurrentState() {
        if (!m_displayInformation || !m_changeHandler)
            return;

        try {
            const auto colorInfo = m_displayInformation.GetAdvancedColorInfo();
            WindowsAdvancedColorState state;
            state.valid = true;
            state.hdrActive =
                colorInfo.CurrentAdvancedColorKind() == AdvancedColorKind::HighDynamicRange;
            state.sdrWhiteNits = colorInfo.SdrWhiteLevelInNits();
            state.minLuminanceNits = colorInfo.MinLuminanceInNits();
            state.maxLuminanceNits = colorInfo.MaxLuminanceInNits();
            state.maxAverageFullFrameLuminanceNits =
                colorInfo.MaxAverageFullFrameLuminanceInNits();
            m_changeHandler(state);
        } catch (const winrt::hresult_error &error) {
            qWarning("Could not refresh Windows HDR display information: 0x%08X %ls",
                     static_cast<unsigned int>(error.code().value),
                     error.message().c_str());
        }
    }

    ChangeHandler m_changeHandler;
    DisplayInformation m_displayInformation{nullptr};
    DispatcherQueueController m_dispatcherController{nullptr};
    winrt::event_token m_changeToken{};
    bool m_hasChangeToken = false;
    bool m_roInitialized = false;
    bool m_roAvailable = false;
};

#else

class WindowsDisplayMonitor::Impl {
public:
    explicit Impl(ChangeHandler) {
    }

    bool attach(QWindow *) {
        return false;
    }

    void detach() {
    }

    void refresh() {
    }
};

#endif

WindowsDisplayMonitor::WindowsDisplayMonitor(ChangeHandler changeHandler)
    : m_impl(std::make_unique<Impl>(std::move(changeHandler))) {
}

WindowsDisplayMonitor::~WindowsDisplayMonitor() = default;

bool WindowsDisplayMonitor::attach(QWindow *window) {
    return m_impl->attach(window);
}

void WindowsDisplayMonitor::detach() {
    m_impl->detach();
}

void WindowsDisplayMonitor::refresh() {
    m_impl->refresh();
}
