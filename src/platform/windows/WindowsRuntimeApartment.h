#pragma once

#include <qt_windows.h>

#include <roapi.h>
#include <winrt/base.h>

// Release all native members before this owner; balance only our initialization.
class WindowsRuntimeApartment final {
  public:
    WindowsRuntimeApartment() {
        HRESULT const result = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
            winrt::check_hresult(result);
        }
        m_initialized = SUCCEEDED(result);
    }
    ~WindowsRuntimeApartment() {
        if (m_initialized) {
            RoUninitialize();
        }
    }
    WindowsRuntimeApartment(WindowsRuntimeApartment const&) = delete;
    WindowsRuntimeApartment& operator=(WindowsRuntimeApartment const&) = delete;

  private:
    bool m_initialized = false;
};
