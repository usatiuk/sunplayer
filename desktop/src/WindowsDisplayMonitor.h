#pragma once

#include <functional>
#include <memory>

class QWindow;

struct WindowsAdvancedColorState {
    bool valid = false;
    bool hdrActive = false;
    float sdrWhiteNits = 80.0f;
    float minLuminanceNits = 0.0f;
    float maxLuminanceNits = 0.0f;
    float maxAverageFullFrameLuminanceNits = 0.0f;
};

class WindowsDisplayMonitor final {
public:
    using ChangeHandler = std::function<void(const WindowsAdvancedColorState &)>;

    explicit WindowsDisplayMonitor(ChangeHandler changeHandler);
    ~WindowsDisplayMonitor();

    WindowsDisplayMonitor(const WindowsDisplayMonitor &) = delete;
    WindowsDisplayMonitor &operator=(const WindowsDisplayMonitor &) = delete;

    bool attach(QWindow *window);
    void detach();
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
