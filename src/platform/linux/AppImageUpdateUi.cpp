#include "platform/linux/AppImageUpdateUi.h"

#include <QCheckBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWindow>

#include "platform/linux/AppImageUpdater.h"

namespace {
constexpr auto checkOnStartupKey = "linux/appimage/checkOnStartup";
}

AppImageUpdateUi::AppImageUpdateUi(AppImageUpdater& updater, QWindow& window, std::function<bool()> mediaEmpty,
                                   QString const& settingsFile)
    : m_updater(updater), m_window(window), m_mediaEmpty(std::move(mediaEmpty)),
      m_settings(settingsFile, QSettings::IniFormat) {
    m_settings.setFallbacksEnabled(false);
    if (m_settings.contains(QLatin1String(checkOnStartupKey))) {
        QString const value = m_settings.value(QLatin1String(checkOnStartupKey)).toString();
        if (value == QStringLiteral("true") || value == QStringLiteral("false")) {
            m_checkOnStartup = value == QStringLiteral("true");
        } else {
            qWarning() << "Invalid AppImage startup-check preference; asking again";
        }
    }
}

AppImageUpdateUi::~AppImageUpdateUi() { delete m_prompt; }

void AppImageUpdateUi::setCheckOnStartup(bool enabled) {
    if (m_checkOnStartup == enabled) {
        return;
    }
    m_checkOnStartup = enabled;
    m_settings.setValue(QLatin1String(checkOnStartupKey), enabled);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        qWarning() << "Could not save AppImage startup-check preference";
    }
    emit preferenceChanged();
    if (enabled) {
        m_updater.check();
    }
}

void AppImageUpdateUi::reconcile() {
    if (!m_started) {
        m_started = true;
        if (m_checkOnStartup.value_or(false)) {
            m_updater.check();
        }
    }
    if (!m_mediaEmpty()) {
        if (m_prompt) {
            m_prompt->close();
        }
        return;
    }
    if (m_checkOnStartup.has_value() || m_promptShown) {
        return;
    }
    m_promptShown = true;
    auto* const prompt = new QDialog;
    m_prompt = prompt;
    prompt->setObjectName(QStringLiteral("appImageUpdateConsent"));
    prompt->setAttribute(Qt::WA_DeleteOnClose);
    prompt->setWindowTitle(tr("Check for updates"));
    prompt->setWindowModality(Qt::WindowModal);
    auto* const layout = new QVBoxLayout(prompt);
    layout->addWidget(new QLabel(tr("Automatically check for updates when SunPlayer starts?"), prompt));
    layout->addWidget(new QLabel(tr("Updates are installed only when you choose.\n"
                                    "You can change this in Settings → Application."),
                                 prompt));
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, prompt);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::clicked, prompt, [this, prompt, buttons](QAbstractButton* button) {
        setCheckOnStartup(buttons->standardButton(button) == QDialogButtonBox::Yes);
        prompt->accept();
    });
    prompt->winId();
    prompt->windowHandle()->setTransientParent(&m_window);
    prompt->open();
}

QWidget* AppImageUpdateUi::createSettingsPage() {
    auto* const page = new QWidget;
    auto* const layout = new QVBoxLayout(page);
    auto* const checkbox = new QCheckBox(tr("Check for updates on startup"), page);
    checkbox->setObjectName(QStringLiteral("appImageCheckOnStartup"));
    checkbox->setChecked(m_checkOnStartup.value_or(false));
    layout->addWidget(checkbox);
    auto* const explanation = new QLabel(tr("Updates are installed only when you choose Update and restart."), page);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    layout->addStretch();
    connect(checkbox, &QCheckBox::toggled, this, &AppImageUpdateUi::setCheckOnStartup);
    connect(this, &AppImageUpdateUi::preferenceChanged, checkbox, [this, checkbox] {
        QSignalBlocker const blocker(checkbox);
        checkbox->setChecked(m_checkOnStartup.value_or(false));
    });
    return page;
}
