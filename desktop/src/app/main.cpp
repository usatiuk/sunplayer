#include <QGuiApplication>
#include <QPalette>

#include "app/PresentationWindow.h"
#include "graphics/GraphicsBackendFactory.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    GraphicsBackendFactory::configureQtQuick();

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#111318")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#f2f4f8")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#171a21")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#20242d")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#f2f4f8")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#252a35")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#f2f4f8")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#4f8cff")));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#8e97a8")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#252a35")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#f2f4f8")));
    app.setPalette(palette);

    PresentationWindow window;
    window.show();

    return app.exec();
}
