#include <QtTest>

#include "app/VideoViewportState.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class VideoViewportStateTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void visibilityAndGeometryDefineRenderability();
    void unchangedValuesDoNotNotify();
};

void VideoViewportStateTest::visibilityAndGeometryDefineRenderability() {
    VideoViewportState viewport(nullptr);
    QSignalSpy changes(&viewport, &VideoViewportState::viewportChanged);

    QCOMPARE(viewport.rect(), QRectF(0.0, 0.0, 0.0, 0.0));
    QVERIFY(!viewport.visible());
    QVERIFY(!viewport.isRenderable());

    QRectF const rect(24.0, 112.0, 800.0, 500.0);
    viewport.setRect(rect);
    QCOMPARE(viewport.rect(), rect);
    QVERIFY(!viewport.isRenderable());

    viewport.setVisible(true);
    QVERIFY(viewport.isRenderable());

    viewport.setRect(QRectF(24.0, 112.0, 0.0, 500.0));
    QVERIFY(!viewport.isRenderable());
    QCOMPARE(changes.count(), 3);
}

void VideoViewportStateTest::unchangedValuesDoNotNotify() {
    VideoViewportState viewport(nullptr);
    QSignalSpy changes(&viewport, &VideoViewportState::viewportChanged);

    viewport.setRect(viewport.rect());
    viewport.setVisible(viewport.visible());
    QCOMPARE(changes.count(), 0);
}

QTEST_APPLESS_MAIN(VideoViewportStateTest)
#include "tst_VideoViewportState.moc"
