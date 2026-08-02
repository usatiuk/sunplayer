#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// Presentation-target controls used by the current diagnostic application.
class PresentationSettings final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("PresentationSettings is owned by the application")

    Q_PROPERTY(bool automaticTargetPeak READ automaticTargetPeak
               WRITE setAutomaticTargetPeak NOTIFY settingsChanged)
    Q_PROPERTY(float manualTargetHeadroom READ manualTargetHeadroom
               WRITE setManualTargetHeadroom NOTIFY settingsChanged)

public:
    explicit PresentationSettings(QObject *parent);

    bool automaticTargetPeak() const;
    float manualTargetHeadroom() const;

    void setAutomaticTargetPeak(bool value);
    void setManualTargetHeadroom(float value);

signals:
    void settingsChanged();

private:
    float m_manualTargetHeadroom = 7.5f;
    bool m_automaticTargetPeak = true;
};
