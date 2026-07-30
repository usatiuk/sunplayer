#include <memory>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QVariant>

#include "QmlShellTestTypes.h"
#include "app/VideoViewportState.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
QString componentErrorText(const QQmlComponent &component) {
    QStringList messages;
    for (const QQmlError &error : component.errors())
        messages.append(error.toString());
    return messages.join(u'\n');
}
}

class AppShellTest final : public QObject {
    Q_OBJECT

private slots:
    void publishesActiveViewport();

public:
    static void initMain() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
#ifdef Q_OS_WIN
        SetErrorMode(
            SEM_FAILCRITICALERRORS
            | SEM_NOGPFAULTERRORBOX
            | SEM_NOOPENFILEERRORBOX);
#endif
    }
};

void AppShellTest::publishesActiveViewport() {
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(
        SUNROOM_QT_QML_IMPORT_PATH));
    QQmlComponent component(&engine);
    component.loadFromModule(
        QStringLiteral("SunroomShellTest"), QStringLiteral("Main"));
    QVERIFY2(
        !component.isError(),
        qPrintable(componentErrorText(component)));

    ShellTestPresentationOutputState outputState(nullptr);
    ShellTestPresentationSettings presentationSettings(nullptr);
    ShellTestDiagnosticVideoSource videoSource(nullptr);
    ShellTestMediaSession mediaSession(nullptr);
    ShellTestActiveVideoSource activeVideoSource(nullptr);
    VideoViewportState videoViewport(nullptr);
    const QVariantMap initialProperties{
        {
            QStringLiteral("presentationOutput"),
            QVariant::fromValue(&outputState),
        },
        {
            QStringLiteral("presentationPolicy"),
            QVariant::fromValue(&presentationSettings),
        },
        {
            QStringLiteral("diagnosticSource"),
            QVariant::fromValue(&videoSource),
        },
        {
            QStringLiteral("mediaSession"),
            QVariant::fromValue(&mediaSession),
        },
        {
            QStringLiteral("activeVideoSource"),
            QVariant::fromValue(&activeVideoSource),
        },
        {
            QStringLiteral("viewportState"),
            QVariant::fromValue(&videoViewport),
        },
    };

    std::unique_ptr<QObject> object(
        component.createWithInitialProperties(initialProperties));
    QVERIFY2(object, qPrintable(componentErrorText(component)));
    QQuickItem *const rootItem = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(rootItem);
    QObject *const rendererSwitch =
        rootItem->findChild<QObject *>(
            QStringLiteral("videoRendererSwitch"));
    QVERIFY(rendererSwitch);
    QObject *const emptyState =
        rootItem->findChild<QObject *>(
            QStringLiteral("emptyState"));
    QObject *const openingState =
        rootItem->findChild<QObject *>(
            QStringLiteral("openingState"));
    QObject *const errorState =
        rootItem->findChild<QObject *>(
            QStringLiteral("errorState"));
    QObject *const cancelOpenButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("cancelOpenButton"));
    QObject *const retryMediaButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("retryMediaButton"));
    QObject *const closeMediaButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("closeMediaButton"));
    QObject *const playPauseButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("playPauseButton"));
    QObject *const seekingState =
        rootItem->findChild<QObject *>(
            QStringLiteral("seekingState"));
    QObject *const seekSlider =
        rootItem->findChild<QObject *>(
            QStringLiteral("seekSlider"));
    QObject *const positionLabel =
        rootItem->findChild<QObject *>(
            QStringLiteral("positionLabel"));
    QObject *const durationLabel =
        rootItem->findChild<QObject *>(
            QStringLiteral("durationLabel"));
    QVERIFY(emptyState);
    QVERIFY(openingState);
    QVERIFY(errorState);
    QVERIFY(cancelOpenButton);
    QVERIFY(retryMediaButton);
    QVERIFY(closeMediaButton);
    QVERIFY(playPauseButton);
    QVERIFY(seekingState);
    QVERIFY(seekSlider);
    QVERIFY(positionLabel);
    QVERIFY(durationLabel);
    QCOMPARE(
        rendererSwitch->property("checked").toBool(),
        true);
    QCOMPARE(
        activeVideoSource.route(),
        ShellTestActiveVideoSource::Route::Player);

    QQuickWindow quickWindow;
    rootItem->setParentItem(quickWindow.contentItem());
    rootItem->setSize({1100.0, 760.0});
    quickWindow.resize(1100, 760);
    quickWindow.show();
    QTRY_VERIFY(quickWindow.isExposed());
    QTRY_VERIFY(!videoViewport.visible());
    QTRY_VERIFY(emptyState->property("visible").toBool());
    QVERIFY(!openingState->property("visible").toBool());
    QVERIFY(!errorState->property("visible").toBool());
    QVERIFY(!seekSlider->property("visible").toBool());

    mediaSession.setState(
        ShellTestMediaSession::State::Opening);
    QTRY_VERIFY(openingState->property("visible").toBool());
    QVERIFY(!videoViewport.visible());
    QVERIFY(QMetaObject::invokeMethod(
        cancelOpenButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.cancelCount(), 1);
    QCOMPARE(
        mediaSession.state(),
        ShellTestMediaSession::State::Empty);

    mediaSession.setState(
        ShellTestMediaSession::State::Ready, true);
    mediaSession.setState(
        ShellTestMediaSession::State::Error);
    QTRY_VERIFY(errorState->property("visible").toBool());
    QVERIFY(retryMediaButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(
        retryMediaButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.retryCount(), 1);
    QCOMPARE(mediaSession.openCount(), 0);

    mediaSession.setState(
        ShellTestMediaSession::State::Ready, true);
    QTRY_VERIFY(videoViewport.visible());
    QTRY_COMPARE(videoViewport.rect().x(), 24.0);
    QTRY_COMPARE(videoViewport.rect().y(), 128.0);
    QTRY_COMPARE(videoViewport.rect().width(), 1052.0);
    QTRY_COMPARE(videoViewport.rect().height(), 552.0);
    QTRY_VERIFY(seekSlider->property("visible").toBool());
    QVERIFY(seekSlider->property("enabled").toBool());
    QCOMPARE(seekSlider->property("from").toDouble(), 0.0);
    QCOMPARE(seekSlider->property("to").toDouble(), 65'000.0);
    QCOMPARE(durationLabel->property("text").toString(),
             QStringLiteral("1:05"));

    mediaSession.setTimeline(12'500, 65'000, true);
    QTRY_COMPARE(
        seekSlider->property("value").toDouble(),
        12'500.0);
    QCOMPARE(positionLabel->property("text").toString(),
             QStringLiteral("0:12"));
    QCOMPARE(mediaSession.seekCount(), 0);
    QQuickItem *const seekSliderItem =
        qobject_cast<QQuickItem *>(seekSlider);
    QVERIFY(seekSliderItem);
    const QPointF start =
        seekSliderItem->mapToScene({
            seekSliderItem->width() * 0.2,
            seekSliderItem->height() * 0.5,
        });
    const QPointF destination =
        seekSliderItem->mapToScene({
            seekSliderItem->width() * 0.7,
            seekSliderItem->height() * 0.5,
        });
    QTest::mousePress(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        start.toPoint());
    QTest::mouseMove(
        &quickWindow,
        destination.toPoint());
    QCOMPARE(mediaSession.seekCount(), 0);
    QTest::mouseRelease(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        destination.toPoint());
    QTRY_COMPARE(mediaSession.seekCount(), 1);
    QVERIFY(mediaSession.lastSeekMilliseconds() > 30'000);
    QTRY_COMPARE(
        qRound(seekSlider->property("value").toDouble()),
        static_cast<int>(
            mediaSession.lastSeekMilliseconds()));

    mediaSession.setTimeline(42'000, 65'000, false);
    QTRY_VERIFY(!seekSlider->property("enabled").toBool());
    mediaSession.setTimeline(42'000, 65'000, true);
    QTRY_VERIFY(seekSlider->property("enabled").toBool());

    mediaSession.setState(
        ShellTestMediaSession::State::Opening);
    mediaSession.setTimeline(42'000, 65'000, true, true);
    QTRY_VERIFY(seekingState->property("visible").toBool());
    QVERIFY(!openingState->property("visible").toBool());
    QVERIFY(!seekSlider->property("enabled").toBool());
    QVERIFY(!videoViewport.visible());
    mediaSession.setState(
        ShellTestMediaSession::State::Ready, true);
    QVERIFY(mediaSession.playing());
    QVERIFY(QMetaObject::invokeMethod(
        playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(!mediaSession.playing());
    QVERIFY(QMetaObject::invokeMethod(
        playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(mediaSession.playing());

    const qreal originalHeight = videoViewport.rect().height();
    rootItem->setSize({900.0, 650.0});
    QTRY_COMPARE(videoViewport.rect().width(), 852.0);
    QTRY_VERIFY(videoViewport.rect().height() < originalHeight);

    QVERIFY(QMetaObject::invokeMethod(
        closeMediaButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.cancelCount(), 2);
    QTRY_VERIFY(!videoViewport.visible());
    mediaSession.setState(
        ShellTestMediaSession::State::Ready, true);

    rootItem->setProperty("currentPage", 1);
    QTRY_COMPARE(
        activeVideoSource.route(),
        ShellTestActiveVideoSource::Route::Diagnostics);
    QTRY_COMPARE(videoViewport.rect().x(), 24.0);
    QTRY_COMPARE(videoViewport.rect().y(), 168.0);
    QTRY_COMPARE(videoViewport.rect().width(), 852.0);

    rootItem->setVisible(false);
    QTRY_VERIFY(!videoViewport.visible());
    rootItem->setVisible(true);
    QTRY_VERIFY(videoViewport.visible());

    videoSource.setUseLibplacebo(false);
    QTRY_COMPARE(
        rendererSwitch->property("checked").toBool(),
        false);
}

QTEST_MAIN(AppShellTest)

#include "tst_AppShell.moc"
