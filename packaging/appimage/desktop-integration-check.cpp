#include <QApplication>
#include <QIcon>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTimer>

#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    for (auto const* name :
         {"window-minimize-symbolic", "window-maximize-symbolic", "window-restore-symbolic", "window-close-symbolic"}) {
        auto const icon = QIcon::fromTheme(QString::fromLatin1(name));
        if (icon.isNull() || icon.pixmap(16, 16).isNull()) {
            qCritical("Could not render system theme icon: %s", name);
            return 1;
        }
    }
    qInfo() << "System window icons rendered from theme" << QIcon::themeName();

    if (app.arguments().contains(QStringLiteral("--file-dialog"))) {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Dialogs
            Window {
                visible: true
                width: 320
                height: 100
                title: "SunPlayer desktop integration check"
                FileDialog { id: picker; title: "SunPlayer portal check" }
                // Model opening after startup, when Qt has queried portal capabilities.
                Timer { interval: 250; running: true; onTriggered: picker.open() }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> window(component.create());
        if (!window) {
            qCritical() << component.errors();
            return 1;
        }
        // Bound the interactive probe; portal dispatch is observed on the session bus.
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
        return app.exec();
    }
    return 0;
}
