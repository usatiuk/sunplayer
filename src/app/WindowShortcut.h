#pragma once

#include <optional>

#include <QKeyEvent>

inline std::optional<qlonglong> relativeSeekMilliseconds(QKeyEvent const& event) {
    Qt::KeyboardModifiers const modifiers = event.modifiers();
    if (modifiers != Qt::NoModifier && modifiers != Qt::KeypadModifier) {
        return std::nullopt;
    }

    if (event.key() == Qt::Key_Left) {
        return -10'000;
    }
    if (event.key() == Qt::Key_Right) {
        return 10'000;
    }
    return std::nullopt;
}
