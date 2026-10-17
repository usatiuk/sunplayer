#pragma once

#include <functional>
#include <optional>

#include <QObject>
#include <QPointer>
#include <QSettings>

class AppImageUpdater;
class QDialog;
class QWidget;
class QWindow;

// Consent and native widgets exist only alongside the Linux AppImage capability.
class AppImageUpdateUi final : public QObject {
    Q_OBJECT

  public:
    AppImageUpdateUi(AppImageUpdater& updater, QWindow& window, std::function<bool()> mediaEmpty,
                     QString const& settingsFile);
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
    QSettings m_settings;
    std::optional<bool> m_checkOnStartup;
    QPointer<QDialog> m_prompt;
    bool m_started = false;
    bool m_promptShown = false;
};
