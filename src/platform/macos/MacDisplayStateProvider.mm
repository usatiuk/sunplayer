#include "platform/macos/MacDisplayStateProvider.h"

#include <algorithm>
#include <cmath>

#import <AppKit/AppKit.h>

#include <QPointer>
#include <QScreen>
#include <QWindow>
#include <QtGui/qscreen_platform.h>

#include "diagnostics/LogCategories.h"
#include "platform/DisplayStateProvider.h"

namespace {
constexpr ColorPrimaries bt709Primaries{
    .red = {0.640f, 0.330f},
    .green = {0.300f, 0.600f},
    .blue = {0.150f, 0.060f},
    .white = {0.3127f, 0.3290f},
};

constexpr ColorPrimaries displayP3Primaries{
    .red = {0.680f, 0.320f},
    .green = {0.265f, 0.690f},
    .blue = {0.150f, 0.060f},
    .white = {0.3127f, 0.3290f},
};

float headroomOrOne(CGFloat value, char const* name) {
    float const headroom = static_cast<float>(value);
    if (std::isfinite(headroom) && headroom >= 1.0f) {
        return headroom;
    }
    qCWarning(sunplayerLogPlatform, "macOS reported invalid %s: %g; using 1", name, static_cast<double>(value));
    return 1.0f;
}

class MacDisplayStateProvider final : public DisplayStateProvider {
  public:
    explicit MacDisplayStateProvider(QObject* parent) : DisplayStateProvider(parent) {}

    ~MacDisplayStateProvider() override { detach(); }

    void attach(QWindow& window) override {
        detach();
        m_window = &window;
        m_observer =
            [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidChangeScreenParametersNotification
                                                              object:nil
                                                               queue:[NSOperationQueue mainQueue]
                                                          usingBlock:^(NSNotification*) {
                                                            if (m_window) {
                                                                refresh();
                                                            }
                                                          }];
        m_colorSpaceObserver =
            [[NSNotificationCenter defaultCenter] addObserverForName:NSScreenColorSpaceDidChangeNotification
                                                              object:nil
                                                               queue:[NSOperationQueue mainQueue]
                                                          usingBlock:^(NSNotification*) {
                                                            if (m_window) {
                                                                refresh();
                                                            }
                                                          }];
        refresh();
    }

    void detach() override {
        if (m_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:m_observer];
            m_observer = nil;
        }
        if (m_colorSpaceObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:m_colorSpaceObserver];
            m_colorSpaceObserver = nil;
        }
        m_window.clear();
    }

    void refresh() override {
        if (!m_window || !m_window->screen()) {
            emit stateChanged(DisplayState{});
            return;
        }
        auto* const cocoaScreen = m_window->screen()->nativeInterface<QNativeInterface::QCocoaScreen>();
        NSScreen* const screen = cocoaScreen ? cocoaScreen->nativeScreen() : nil;
        if (!screen) {
            emit stateChanged(DisplayState{});
            return;
        }

        DisplayState state;
        state.valid = true;
        state.currentHeadroom =
            headroomOrOne(screen.maximumExtendedDynamicRangeColorComponentValue, "current EDR headroom");
        state.potentialHeadroom = std::max(
            state.currentHeadroom,
            headroomOrOne(screen.maximumPotentialExtendedDynamicRangeColorComponentValue, "potential EDR headroom"));
        state.colorMode = state.potentialHeadroom > 1.0f ? DisplayColorMode::HighDynamicRange
                                                         : DisplayColorMode::StandardDynamicRange;
        state.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
        if ([screen canRepresentDisplayGamut:NSDisplayGamutP3]) {
            state.targetPrimariesKnown = true;
            state.targetPrimaries = displayP3Primaries;
        } else if ([screen canRepresentDisplayGamut:NSDisplayGamutSRGB]) {
            state.targetPrimariesKnown = true;
            state.targetPrimaries = bt709Primaries;
        }
        emit stateChanged(state);
    }

  private:
    QPointer<QWindow> m_window;
    id m_observer = nil;
    id m_colorSpaceObserver = nil;
};
} // namespace

std::unique_ptr<DisplayStateProvider> createMacDisplayStateProvider(QObject* parent) {
    return std::make_unique<MacDisplayStateProvider>(parent);
}
