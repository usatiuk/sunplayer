#pragma once

#include <QColor>
#include <QObject>

#include "subtitles/SubtitleAppearance.h"

class SubtitleSettings : public QObject {
    Q_OBJECT

    Q_PROPERTY(AppearanceMode appearanceMode READ appearanceMode WRITE setAppearanceMode NOTIFY settingsChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY settingsChanged)
    Q_PROPERTY(qreal textOpacity READ textOpacity WRITE setTextOpacity NOTIFY settingsChanged)
    Q_PROPERTY(bool backgroundEnabled READ backgroundEnabled WRITE setBackgroundEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY settingsChanged)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY settingsChanged)
    Q_PROPERTY(EdgeStyle edgeStyle READ edgeStyle WRITE setEdgeStyle NOTIFY settingsChanged)
    Q_PROPERTY(QColor edgeColor READ edgeColor WRITE setEdgeColor NOTIFY settingsChanged)
    Q_PROPERTY(qreal edgeOpacity READ edgeOpacity WRITE setEdgeOpacity NOTIFY settingsChanged)
    Q_PROPERTY(SizeMode sizeMode READ sizeMode WRITE setSizeMode NOTIFY settingsChanged)
    Q_PROPERTY(qreal scale READ scale WRITE setScale NOTIFY settingsChanged)
    Q_PROPERTY(PositionMode positionMode READ positionMode WRITE setPositionMode NOTIFY settingsChanged)
    Q_PROPERTY(qreal verticalPosition READ verticalPosition WRITE setVerticalPosition NOTIFY settingsChanged)
    Q_PROPERTY(qreal overallOpacity READ overallOpacity WRITE setOverallOpacity NOTIFY settingsChanged)
    Q_PROPERTY(qulonglong rasterRevision READ rasterRevision NOTIFY settingsChanged)

  public:
    enum AppearanceMode { AuthoredAppearance, CustomAppearance };
    Q_ENUM(AppearanceMode)
    enum EdgeStyle { NoEdge, Outline, Shadow };
    Q_ENUM(EdgeStyle)
    enum SizeMode { AuthoredSize, CustomSize };
    Q_ENUM(SizeMode)
    enum PositionMode { AuthoredPosition, CustomPosition };
    Q_ENUM(PositionMode)

    explicit SubtitleSettings(QObject* parent = nullptr);

    AppearanceMode appearanceMode() const;
    QColor textColor() const;
    qreal textOpacity() const;
    bool backgroundEnabled() const;
    QColor backgroundColor() const;
    qreal backgroundOpacity() const;
    EdgeStyle edgeStyle() const;
    QColor edgeColor() const;
    qreal edgeOpacity() const;
    SizeMode sizeMode() const;
    qreal scale() const;
    PositionMode positionMode() const;
    qreal verticalPosition() const;
    qreal overallOpacity() const;
    qulonglong rasterRevision() const;

    void setAppearanceMode(AppearanceMode value);
    void setTextColor(QColor value);
    void setTextOpacity(qreal value);
    void setBackgroundEnabled(bool value);
    void setBackgroundColor(QColor value);
    void setBackgroundOpacity(qreal value);
    void setEdgeStyle(EdgeStyle value);
    void setEdgeColor(QColor value);
    void setEdgeOpacity(qreal value);
    void setSizeMode(SizeMode value);
    void setScale(qreal value);
    void setPositionMode(PositionMode value);
    void setVerticalPosition(qreal value);
    void setOverallOpacity(qreal value);

    SubtitleAppearanceValues values() const;
    SubtitleAppearanceSnapshot snapshot() const;
    void restore(SubtitleAppearanceValues const& values);

    Q_INVOKABLE void applyAsAuthored();
    Q_INVOKABLE void applyHighContrast();
    Q_INVOKABLE void applyLargeText();
    Q_INVOKABLE void restoreDefaults();

  signals:
    void settingsChanged();
    void persistenceChanged(SubtitleAppearanceFields dirtyFields);
    void persistenceResetRequested();

  private:
    void apply(SubtitleAppearanceValues values, bool persist, bool reset);
    void changed(SubtitleAppearanceFields dirtyField, bool rasterAffecting);

    SubtitleAppearanceValues m_values;
    std::uint64_t m_rasterRevision = 1;
};
