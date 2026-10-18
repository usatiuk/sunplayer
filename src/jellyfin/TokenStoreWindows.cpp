#include "diagnostics/LogCategories.h"
#include "jellyfin/TokenStore.h"

#include "platform/windows/WindowsRuntimeApartment.h"
#include <qt_windows.h>
#include <winrt/Windows.Security.Credentials.h>

using winrt::Windows::Security::Credentials::PasswordCredential;
using winrt::Windows::Security::Credentials::PasswordVault;

namespace {
constexpr auto resource = L"SunPlayer";

template <typename Operation> QString withVault(char const* action, QString const& failure, Operation operation) {
    try {
        // Native objects must die before this apartment, independently of any
        // initialization owned by the player or its native media controls.
        WindowsRuntimeApartment apartment;
        operation(PasswordVault{});
        qCInfo(sunplayerLogApplication, "event=jellyfin.token_store backend=password_vault operation=%s result=ok",
               action);
        return {};
    } catch (winrt::hresult_error const& error) {
        qCWarning(sunplayerLogApplication,
                  "event=jellyfin.token_store backend=password_vault operation=%s hresult=0x%08X", action,
                  static_cast<unsigned int>(error.code().value));
        // Native error messages can include credential identifiers. Keep only
        // the HRESULT alongside the user-facing operation failure.
        return failure + QStringLiteral(" (0x%1)").arg(quint32(error.code().value), 8, 16, QLatin1Char('0'));
    }
}
} // namespace

void TokenStore::read(std::function<void(QString, QString)> done) {
    QMetaObject::invokeMethod(
        this,
        [this, done = std::move(done)] {
            QString token;
            auto const error = withVault(
                "read", tr("Secure storage is unavailable. Please sign in again."), [&](PasswordVault const& vault) {
                    auto credential = vault.Retrieve(resource, winrt::hstring(m_entry.toStdWString()));
                    credential.RetrievePassword();
                    auto const password = credential.Password();
                    token = QString::fromWCharArray(password.c_str(), password.size());
                });
            done(std::move(token), error);
        },
        Qt::QueuedConnection);
}

void TokenStore::write(QString const& token, std::function<void(QString)> done) {
    Q_ASSERT(!token.isEmpty());
    QMetaObject::invokeMethod(
        this,
        [this, token, done = std::move(done)] {
            auto const error =
                withVault("write", tr("Login could not be saved securely. This login is session-only."),
                          [&](PasswordVault const& vault) {
                              // Add replaces the same resource/entry without deleting the old token first.
                              vault.Add(PasswordCredential(resource, winrt::hstring(m_entry.toStdWString()),
                                                           winrt::hstring(token.toStdWString())));
                          });
            done(error);
        },
        Qt::QueuedConnection);
}

void TokenStore::remove(std::function<void(QString)> done) {
    QMetaObject::invokeMethod(
        this,
        [this, done = std::move(done)] {
            auto const error = withVault(
                "remove", tr("The saved token could not be deleted. Check secure storage before signing in again."),
                [&](PasswordVault const& vault) {
                    try {
                        vault.Remove(vault.Retrieve(resource, winrt::hstring(m_entry.toStdWString())));
                    } catch (winrt::hresult_error const& error) {
                        if (error.code() != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                            throw;
                        }
                    }
                });
            done(error);
        },
        Qt::QueuedConnection);
}
