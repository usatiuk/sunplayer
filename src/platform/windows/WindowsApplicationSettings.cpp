#include "app/ApplicationSettings.h"

#include <QDir>
#include <QScopeGuard>
#include <qt_windows.h>

#include <appmodel.h>
#include <roapi.h>
#include <winrt/Windows.Storage.h>

#include "diagnostics/LogCategories.h"

namespace {
QSettings defaultSettings() {
    UINT32 length = 0;
    LONG const result = GetCurrentPackageFullName(&length, nullptr);
    if (result == APPMODEL_ERROR_NO_PACKAGE) {
        return QSettings{};
    }
    if (result != ERROR_INSUFFICIENT_BUFFER) {
        qFatal("Cannot query settings package identity: error=%ld", result);
    }

    try {
        HRESULT const initialized = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
            winrt::check_hresult(initialized);
        }
        auto apartment = qScopeGuard([initialized] {
            if (SUCCEEDED(initialized)) {
                RoUninitialize();
            }
        });
        // Resolve package-owned storage instead of relying on registry virtualization.
        auto const folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
        auto const nativePath = folder.Path();
        QString const path = QString::fromWCharArray(nativePath.c_str(), nativePath.size());
        if (path.isEmpty()) {
            qFatal("Package settings LocalFolder path is empty");
        }
        qCInfo(sunplayerLogApplication, "event=application.settings_store backend=package_localfolder");
        return QSettings(QDir(path).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    } catch (winrt::hresult_error const& error) {
        qFatal("Cannot resolve package settings LocalFolder: hresult=0x%08X",
               static_cast<unsigned int>(error.code().value));
    }
}
} // namespace

ApplicationSettings::ApplicationSettings() : m_settings(defaultSettings()) { m_settings.setFallbacksEnabled(false); }
