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
float headroomOrOne(CGFloat value, const char *name) {
    const float headroom = static_cast<float>(value);
    if (std::isfinite(headroom) && headroom >= 1.0f)
        return headroom;
    qCWarning(
        sunplayerLogPlatform,
        "macOS reported invalid %s: %g; using 1",
        name,
        static_cast<double>(value));
    return 1.0f;
}

class MacDisplayStateProvider final : public DisplayStateProvider {
public:
    explicit MacDisplayStateProvider(QObject *parent)
        : DisplayStateProvider(parent) {}

    ~MacDisplayStateProvider() override {
        detach();
    }

    void attach(QWindow &window) override {
        detach();
        m_window = &window;
        m_observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:
                NSApplicationDidChangeScreenParametersNotification
            object:nil
            queue:[NSOperationQueue mainQueue]
            usingBlock:^(NSNotification *) {
                if (m_window)
                    refresh();
            }];
        refresh();
    }

    void detach() override {
        if (m_observer) {
            [[NSNotificationCenter defaultCenter]
                removeObserver:m_observer];
            m_observer = nil;
        }
        m_window.clear();
    }

    void refresh() override {
        if (!m_window || !m_window->screen()) {
            emit stateChanged(DisplayState{});
            return;
        }
        auto *const cocoaScreen =
            m_window->screen()->nativeInterface<
                QNativeInterface::QCocoaScreen>();
        NSScreen *const screen = cocoaScreen
            ? cocoaScreen->nativeScreen()
            : nil;
        if (!screen) {
            emit stateChanged(DisplayState{});
            return;
        }

        DisplayState state;
        state.valid = true;
        state.currentHeadroom = headroomOrOne(
            screen.maximumExtendedDynamicRangeColorComponentValue,
            "current EDR headroom");
        state.potentialHeadroom = std::max(
            state.currentHeadroom,
            headroomOrOne(
                screen.maximumPotentialExtendedDynamicRangeColorComponentValue,
                "potential EDR headroom"));
        state.hdrActive = state.potentialHeadroom > 1.0f;
        emit stateChanged(state);
    }

private:
    QPointer<QWindow> m_window;
    id m_observer = nil;
};
}

std::unique_ptr<DisplayStateProvider>
createMacDisplayStateProvider(QObject *parent) {
    return std::make_unique<MacDisplayStateProvider>(parent);
}
