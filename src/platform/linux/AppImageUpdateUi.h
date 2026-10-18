#pragma once

#include <functional>
#include <optional>

#include "settings/SettingsStore.h"
#include <QObject>
#include <QPointer>

class AppImageUpdater;
class QDialog;
class QWidget;
class QWindow;

// Consent and native widgets exist only alongside the Linux AppImage capability.
class AppImageUpdateUi final : public QObject {
    Q_OBJECT

  public:
    AppImageUpdateUi(AppImageUpdater& updater, QWindow& window, std::function<bool()> mediaEmpty,
                     SettingsStore& settings);
    ~AppImageUpdateUi() override;
    void reconcile();
    QWidget* createSettingsPage();

  signals:
    void preferenceChanged();

  private:
    void setCheckOnStartup(bool enabled);

    AppImageUpdater& m_updater;
    QWindow& m_window;
    std::function<bool()> const m_mediaEmpty;
    SettingsStore& m_settings;
    std::optional<bool> m_checkOnStartup;
    QPointer<QDialog> m_prompt;
    bool m_started = false;
    bool m_promptShown = false;
};
