#include <QtTest>

#include <QBackingStore>
#include <QGuiApplication>
#include <QPainter>
#include <QProcess>
#include <QSocketNotifier>
#include <QTemporaryDir>
#include <QWindow>
#include <QtWaylandCompositor/QWaylandBufferRef>
#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandOutput>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QtWaylandCompositor/QWaylandView>
#include <QtWaylandCompositor/QWaylandXdgShell>
#include <wayland-server-protocol.h>

#include <cstdio>

#include "platform/linux/WaylandWindowShadow.h"

namespace {
class ShadowHost final : public QWindow {
  public:
    ShadowHost() : backingStore(this) {
        setFlags(Qt::Window | Qt::FramelessWindowHint);
        resize(320, 200);
        create();
        shadow = std::make_unique<WaylandWindowShadow>(
            *this, [this](bool tiled) { setTitle(tiled ? QStringLiteral("tiled") : QStringLiteral("floating")); });
    }

    ~ShadowHost() override { shadow.reset(); }

  protected:
    bool event(QEvent* event) override {
        bool const result = QWindow::event(event);
        if ((event->type() == QEvent::Expose || event->type() == QEvent::UpdateRequest ||
             event->type() == QEvent::Paint) &&
            isExposed()) {
            backingStore.resize(size());
            QRect const rect(QPoint{}, size());
            backingStore.beginPaint(rect);
            {
                QPainter painter(backingStore.paintDevice());
                painter.fillRect(rect, Qt::blue);
            }
            backingStore.endPaint();
            backingStore.flush(rect);
        }
        return result;
    }

  private:
    QBackingStore backingStore;
    std::unique_ptr<WaylandWindowShadow> shadow;
};

// A real Wayland server view: copy SHM pixels before releasing the old frame.
class CaptureView final : public QWaylandView {
  public:
    using QWaylandView::QWaylandView;
    void bufferCommitted(QWaylandBufferRef const& buffer, QRegion const& damage) override {
        image = buffer.image().copy();
        if (retainBuffers && !buffer.isNull()) {
            retained.push_back(buffer);
        }
        QWaylandView::bufferCommitted(buffer, damage);
        advance();
        surface()->frameStarted();
        surface()->sendFrameCallbacks();
    }
    QImage image;
    bool retainBuffers = false;
    QList<QWaylandBufferRef> retained;
};
} // namespace

class WaylandWindowShadowTest final : public QObject {
    Q_OBJECT
  private slots:
    void nativeShadowLifecycle();
};

void WaylandWindowShadowTest::nativeShadowLifecycle() {
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    QByteArray const previousRuntime = qgetenv("XDG_RUNTIME_DIR");
    qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
    auto restoreRuntime = qScopeGuard([previousRuntime] { qputenv("XDG_RUNTIME_DIR", previousRuntime); });
    QWaylandCompositor compositor;
    compositor.setSocketName("shadow-test");
    QWaylandXdgShell shell(&compositor);
    QWaylandOutput output(&compositor, nullptr);
    QWaylandOutputMode const mode(QSize(1280, 800), 60000);
    output.addMode(mode, true);
    output.setCurrentMode(mode);
    compositor.create();
    QVERIFY(compositor.isCreated());

    QPointer<QWaylandXdgToplevel> toplevel;
    QPointer<QWaylandXdgSurface> body;
    QPointer<QWaylandSurface> child;
    CaptureView* shadowView = nullptr;
    QPoint shadowPosition;
    QPointer<QWaylandSurface> stackedAboveShadow;
    connect(&compositor, &QWaylandCompositor::surfaceCreated, this, [&](QWaylandSurface* surface) {
        auto* view = new CaptureView(nullptr, surface);
        view->setSurface(surface);
        view->setOutput(&output);
        connect(surface, &QWaylandSurface::parentChanged, this,
                [&, view, surface](QWaylandSurface* parent, QWaylandSurface*) {
                    if (parent) {
                        child = surface;
                        shadowView = view;
                    }
                });
        connect(surface, &QWaylandSurface::subsurfacePositionChanged, this,
                [&](QPoint const& position) { shadowPosition = position; });
        connect(surface, &QWaylandSurface::subsurfacePlaceBelow, this,
                [&](QWaylandSurface* sibling) { stackedAboveShadow = sibling; });
    });
    connect(&shell, &QWaylandXdgShell::toplevelCreated, this,
            [&](QWaylandXdgToplevel* top, QWaylandXdgSurface* surface) {
                toplevel = top;
                body = surface;
                output.surfaceEnter(surface->surface());
                top->sendConfigure(QSize(320, 200), QList<QWaylandXdgToplevel::State>{});
            });

    QProcess client;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "wayland");
    environment.insert("WAYLAND_DISPLAY", "shadow-test");
    environment.insert("QT_WAYLAND_SHELL_INTEGRATION", "xdg-shell");
    client.setProcessEnvironment(environment);
    client.start(QCoreApplication::applicationFilePath(), {"--shadow-client"});
    QVERIFY(client.waitForStarted());
    auto stopClient = qScopeGuard([&] {
        if (client.state() != QProcess::NotRunning) {
            client.kill();
            client.waitForFinished();
            qWarning().noquote() << client.readAllStandardError().right(14000);
        }
    });
    QTRY_VERIFY_WITH_TIMEOUT(body && child && shadowView && !shadowView->image.isNull(), 5000);
    QTRY_COMPARE(body->windowGeometry(), QRect(0, 0, 320, 200));
    QCOMPARE(shadowPosition, QPoint(-24, -24));
    QCOMPARE(stackedAboveShadow, body->surface());
    QCOMPARE(child->destinationSize(), QSize(368, 248));
    QVERIFY(!child->inputRegionContains(QPoint(24, 100)));
    QVERIFY(!child->inputRegionContains(QPoint(0, 0)));
    QVERIFY(!child->isOpaque());
    QImage const image = shadowView->image;
    int const nearAlpha = image.pixelColor(23, 100).alpha();
    QVERIFY(nearAlpha > 0 && nearAlpha < 255);
    QVERIFY(image.pixelColor(23, 23).alpha() < nearAlpha);
    QCOMPARE(image.pixelColor(23, 23).alpha(), image.pixelColor(image.width() - 24, 23).alpha());
    QVERIFY(image.pixelColor(12, 100).alpha() < nearAlpha);
    QCOMPARE(image.pixelColor(100, 100).alpha(), 0);
    QCOMPARE(image.pixelColor(23, 100).red(), 0);

    toplevel->sendMaximized(QSize(1000, 700));
    compositor.processWaylandEvents();
    QTRY_VERIFY(!child->hasContent());
    QTRY_COMPARE(body->windowGeometry().size(), QSize(1000, 700));
    toplevel->sendFullscreen(QSize(1280, 800));
    compositor.processWaylandEvents();
    QTRY_VERIFY(toplevel->fullscreen());
    QVERIFY(!child->hasContent());
    toplevel->sendUnmaximized(QSize(320, 200));
    compositor.processWaylandEvents();
    QTRY_VERIFY(child->hasContent());
    QTRY_COMPARE(body->windowGeometry().size(), QSize(320, 200));

    // Wire values 5..8 are xdg-shell tiled edges. Keep the same size to prove
    // state-only configures invalidate a paused/idle shadow.
    for (int edge : {5, 6, 7, 8}) {
        toplevel->sendConfigure(QSize(320, 200), QList<int>{edge});
        compositor.processWaylandEvents();
        QTRY_VERIFY(!child->hasContent());
        QTRY_COMPARE(toplevel->title(), QStringLiteral("tiled"));
        toplevel->sendConfigure(QSize(320, 200), QList<int>{});
        compositor.processWaylandEvents();
        QTRY_VERIFY(child->hasContent());
        QTRY_COMPARE(toplevel->title(), QStringLiteral("floating"));
    }
    toplevel->sendConfigure(QSize(480, 300), QList<int>{});
    compositor.processWaylandEvents();
    QTRY_COMPARE(child->destinationSize(), QSize(528, 348));
    QCOMPARE(body->windowGeometry().size(), QSize(480, 300));
    output.setScaleFactor(2);
    wl_surface_send_preferred_buffer_scale(body->surface()->resource(), 2);
    compositor.processWaylandEvents();
    QTRY_COMPARE(child->bufferScale(), 2);
    QTRY_COMPARE(child->bufferSize(), QSize(1056, 696));
    QCOMPARE(body->windowGeometry().size(), QSize(480, 300));

    // Hold both real SHM buffers server-side. Resize pressure must hide the
    // outdated shadow, then converge to the latest size when storage is free.
    shadowView->retainBuffers = true;
    shadowView->retained.push_back(shadowView->currentBuffer());
    toplevel->sendConfigure(QSize(500, 320), QList<int>{});
    compositor.processWaylandEvents();
    QTRY_COMPARE(child->destinationSize(), QSize(548, 368));
    toplevel->sendConfigure(QSize(520, 340), QList<int>{});
    compositor.processWaylandEvents();
    QTRY_VERIFY(!child->hasContent());
    QTRY_COMPARE(body->windowGeometry().size(), QSize(520, 340));
    toplevel->sendFullscreen(QSize(1280, 800));
    compositor.processWaylandEvents();
    QTRY_VERIFY(toplevel->fullscreen());
    QVERIFY(!child->hasContent());
    toplevel->sendUnmaximized(QSize(540, 360));
    compositor.processWaylandEvents();
    QTRY_COMPARE(body->windowGeometry().size(), QSize(540, 360));
    QVERIFY(!child->hasContent());
    shadowView->retainBuffers = false;
    shadowView->retained.clear();
    compositor.processWaylandEvents();
    QTRY_VERIFY(child->hasContent());
    QTRY_COMPARE(child->destinationSize(), QSize(588, 408));

    // Make the runtime storage unavailable in the separate client process.
    // Revisiting the failed size after another successful size must retry.
    client.write("limit-storage\n");
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("ready\n"));
    toplevel->sendConfigure(QSize(560, 380), QList<int>{});
    compositor.processWaylandEvents();
    QTRY_COMPARE(body->windowGeometry().size(), QSize(560, 380));
    QTRY_VERIFY(!child->hasContent());
    client.write("restore-storage\n");
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("ready\n"));
    toplevel->sendConfigure(QSize(580, 400), QList<int>{});
    compositor.processWaylandEvents();
    QTRY_VERIFY(child->hasContent());
    QTRY_COMPARE(child->destinationSize(), QSize(628, 448));
    toplevel->sendConfigure(QSize(560, 380), QList<int>{});
    compositor.processWaylandEvents();
    QTRY_COMPARE(child->destinationSize(), QSize(608, 428));

    QPointer<QWaylandSurface> const oldChild = child;
    client.write("recreate\n");
    QTRY_VERIFY(child && child != oldChild && child->hasContent());
    QTRY_VERIFY(!oldChild || oldChild->isDestroyed());
    toplevel->sendClose();
    compositor.processWaylandEvents();
    QTRY_COMPARE(client.state(), QProcess::NotRunning);
    QCOMPARE(client.exitStatus(), QProcess::NormalExit);
    QCOMPARE(client.exitCode(), 0);
    QVERIFY(client.readAllStandardError().contains("event=wayland.shadow_unavailable reason=storage"));
    QTRY_VERIFY(compositor.surfaces().isEmpty());
}

int main(int argc, char** argv) {
    bool const client = argc > 1 && QByteArray(argv[1]) == "--shadow-client";
    if (!client) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication application(argc, argv);
    if (client) {
        ShadowHost window;
        QByteArray const runtime = qgetenv("XDG_RUNTIME_DIR");
        QByteArray const temporary = qgetenv("TMPDIR");
        QSocketNotifier input(0, QSocketNotifier::Read);
        QFile commands;
        if (!commands.open(0, QIODevice::ReadOnly)) {
            qFatal("Could not open shadow test command pipe");
        }
        QObject::connect(&input, &QSocketNotifier::activated, &window, [&] {
            QByteArray const command = commands.readLine().trimmed();
            if (command == "recreate") {
                window.destroy();
                window.create();
                window.show();
            } else if (command == "limit-storage" || command == "restore-storage") {
                if (command == "limit-storage") {
                    qputenv("XDG_RUNTIME_DIR", "/dev/null");
                    qputenv("TMPDIR", "/dev/null");
                } else {
                    qputenv("XDG_RUNTIME_DIR", runtime);
                    qputenv("TMPDIR", temporary);
                }
                std::puts("ready");
                std::fflush(stdout);
            }
        });
        window.show();
        return application.exec();
    }
    WaylandWindowShadowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_WaylandWindowShadow.moc"
