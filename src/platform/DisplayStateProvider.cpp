#include "platform/DisplayStateProvider.h"

#ifdef Q_OS_MACOS
#include "platform/macos/MacDisplayStateProvider.h"
#elif defined(Q_OS_WIN)
#include "platform/windows/WindowsDisplayStateProvider.h"
#else
namespace {
class NullDisplayStateProvider final : public DisplayStateProvider {
  public:
    explicit NullDisplayStateProvider(QObject* parent) : DisplayStateProvider(parent) {}
    void attach(QWindow&) override { emit stateChanged(DisplayState{}); }
    void detach() override {}
    void refresh() override {}
};
} // namespace
#endif

std::unique_ptr<DisplayStateProvider> createDisplayStateProvider(QObject* parent) {
#ifdef Q_OS_WIN
    return createWindowsDisplayStateProvider(parent);
#elif defined(Q_OS_MACOS)
    return createMacDisplayStateProvider(parent);
#else
    return std::make_unique<NullDisplayStateProvider>(parent);
#endif
}
