#pragma once

#include <memory>

#include <QObject>

#include "platform/DisplayState.h"

class QWindow;

class DisplayStateProvider : public QObject {
    Q_OBJECT

  public:
    explicit DisplayStateProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~DisplayStateProvider() override = default;

    virtual void attach(QWindow& window) = 0;
    virtual void detach() = 0;
    virtual void refresh() = 0;

  signals:
    void stateChanged(DisplayState const& state);
};

std::unique_ptr<DisplayStateProvider> createDisplayStateProvider(QObject* parent = nullptr);
