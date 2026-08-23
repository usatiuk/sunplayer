#include "platform/windows/WindowsDesktopIntegration.h"

#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <qt_windows.h>
#include <shobjidl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include "diagnostics/LogCategories.h"

namespace {
std::optional<bool> currentTokenFlag(TOKEN_INFORMATION_CLASS informationClass) {
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        return std::nullopt;
    }

    DWORD value = 0;
    DWORD returnedLength = 0;
    BOOL const succeeded = GetTokenInformation(rawToken, informationClass, &value, sizeof(value), &returnedLength);
    CloseHandle(rawToken);
    if (!succeeded) {
        return std::nullopt;
    }
    return value != 0;
}
} // namespace

std::optional<QUrl> WindowsDesktopIntegration::pickSingleMediaFile(QWindow& parentWindow) {
    winrt::com_ptr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()));
    if (SUCCEEDED(result)) {
        FILEOPENDIALOGOPTIONS options = 0;
        result = dialog->GetOptions(&options);
        if (SUCCEEDED(result)) {
            result = dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        }
    }
    if (SUCCEEDED(result)) {
        result = dialog->Show(reinterpret_cast<HWND>(parentWindow.winId()));
    }

    winrt::com_ptr<IShellItem> item;
    if (SUCCEEDED(result)) {
        result = dialog->GetResult(item.put());
    }
    if (SUCCEEDED(result) && !item) {
        result = E_UNEXPECTED;
    }

    PWSTR rawPath = nullptr;
    if (SUCCEEDED(result)) {
        result = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    }

    std::optional<QUrl> selectedFile;
    if (SUCCEEDED(result) && rawPath) {
        QString const path = QString::fromWCharArray(rawPath);
        if (path.isEmpty()) {
            result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        } else {
            selectedFile = QUrl::fromLocalFile(path);
        }
    } else if (SUCCEEDED(result)) {
        result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    CoTaskMemFree(rawPath);

    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        qCWarning(sunplayerLogPlatform).nospace()
            << "Windows media picker failed: 0x" << Qt::hex << static_cast<unsigned long>(result);
    }
    return selectedFile;
}

void WindowsDesktopIntegration::openExternalUrl(QObject& callbackContext, QUrl const& url,
                                                std::function<void(bool)> completion) {
    QPointer<QObject> const context(&callbackContext);
    try {
        auto const operation = winrt::Windows::System::Launcher::LaunchUriAsync(
            winrt::Windows::Foundation::Uri(url.toString(QUrl::FullyEncoded).toStdWString()));
        operation.Completed([context, completion = std::move(completion)](
                                winrt::Windows::Foundation::IAsyncOperation<bool> const& completedOperation,
                                winrt::Windows::Foundation::AsyncStatus status) {
            bool opened = false;
            if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
                try {
                    opened = completedOperation.GetResults();
                } catch (...) {}
            }
            if (context) {
                QMetaObject::invokeMethod(
                    context,
                    [context, completion, opened] {
                        if (context) {
                            completion(opened);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });
    } catch (...) { completion(false); }
}

WindowsDesktopIntegration::IsolationState WindowsDesktopIntegration::isolationState() {
    return {
        .appContainer = currentTokenFlag(TokenIsAppContainer),
        .appSilo = currentTokenFlag(TokenIsAppSilo),
    };
}
