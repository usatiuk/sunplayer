#pragma once

#include <functional>
#include <optional>

#include <QUrl>

class QObject;
class QWindow;

namespace WindowsDesktopIntegration {

struct IsolationState {
    std::optional<bool> appContainer;
    std::optional<bool> appSilo;
};

std::optional<QUrl> pickSingleMediaFile(QWindow& parentWindow);
void openExternalUrl(QObject& callbackContext, QUrl const& url, std::function<void(bool)> completion);
IsolationState isolationState();

} // namespace WindowsDesktopIntegration
