#include <algorithm>
#include <memory>

#include <QColor>
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
    ShellTestWindowCommands windowCommands(nullptr);
    ShellTestPresentationSettings presentationSettings(nullptr);
    ShellTestDiagnosticVideoSource videoSource(nullptr);
    ShellTestMediaSession mediaSession(nullptr);
    ShellTestActiveVideoSource activeVideoSource(nullptr);
    VideoViewportState videoViewport(nullptr);
    const QVariantMap initialProperties{
        {
            QStringLiteral("windowCommands"),
            QVariant::fromValue(&windowCommands),
        },
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
    QObject *const playerPage =
        rootItem->findChild<QObject *>(
            QStringLiteral("playerPage"));
    QObject *const playbackHoverHandler =
        rootItem->findChild<QObject *>(
            QStringLiteral("playbackHoverHandler"));
    QQuickItem *const fullscreenBackgroundMouseArea =
        rootItem->findChild<QQuickItem *>(
            QStringLiteral("fullscreenBackgroundMouseArea"));
    QObject *const emptyHdrLabButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("emptyHdrLabButton"));
    QObject *const backToPlayerButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("backToPlayerButton"));
    QQuickItem *const clientSideWindowChrome =
        rootItem->findChild<QQuickItem *>(
            QStringLiteral("clientSideWindowChrome"));
    QQuickItem *const clientSideTitleBar =
        rootItem->findChild<QQuickItem *>(
            QStringLiteral("clientSideTitleBar"));
    QObject *const openingState =
        rootItem->findChild<QObject *>(
            QStringLiteral("openingState"));
    QObject *const waitingForVideoState =
        rootItem->findChild<QObject *>(
            QStringLiteral("waitingForVideoState"));
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
    QObject *const playPauseButtonIcon =
        rootItem->findChild<QObject *>(
            QStringLiteral("playPauseButtonIcon"));
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
    QObject *const muteButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("muteButton"));
    QObject *const volumeSlider =
        rootItem->findChild<QObject *>(
            QStringLiteral("volumeSlider"));
    QObject *const audioDiagnosticsLabel =
        rootItem->findChild<QObject *>(
            QStringLiteral("audioDiagnosticsLabel"));
    QObject *const playbackStateLabel =
        rootItem->findChild<QObject *>(
            QStringLiteral("playbackStateLabel"));
    QObject *const statisticsMenuItem =
        rootItem->findChild<QObject *>(
            QStringLiteral("statisticsMenuItem"));
    QObject *const moreButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("moreButton"));
    QObject *const transportMenu =
        rootItem->findChild<QObject *>(
            QStringLiteral("transportMenu"));
    QObject *const subtitleMenu =
        rootItem->findChild<QObject *>(
            QStringLiteral("subtitleMenu"));
    QObject *const subtitleOffItem =
        rootItem->findChild<QObject *>(
            QStringLiteral("subtitleTrack_-1"));
    QObject *const subtitleEnglishItem =
        rootItem->findChild<QObject *>(
            QStringLiteral("subtitleTrack_2"));
    QObject *const hdrLabMenuItem =
        rootItem->findChild<QObject *>(
            QStringLiteral("hdrLabMenuItem"));
    QObject *const closeStatisticsButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("closeStatisticsButton"));
    QObject *const statisticsPanel =
        rootItem->findChild<QObject *>(
            QStringLiteral("playbackStatisticsPanel"));
    QQuickItem *const transportIsland =
        rootItem->findChild<QQuickItem *>(
            QStringLiteral("transportIsland"));
    QObject *const seekBackwardButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("seekBackwardButton"));
    QObject *const seekForwardButton =
        rootItem->findChild<QObject *>(
            QStringLiteral("seekForwardButton"));
    QVERIFY(emptyState);
    QVERIFY(playerPage);
    QVERIFY(playbackHoverHandler);
    QVERIFY(fullscreenBackgroundMouseArea);
    QVERIFY(emptyHdrLabButton);
    QVERIFY(backToPlayerButton);
    QVERIFY(clientSideWindowChrome);
    QVERIFY(clientSideTitleBar);
    QVERIFY(openingState);
    QVERIFY(waitingForVideoState);
    QVERIFY(errorState);
    QVERIFY(cancelOpenButton);
    QVERIFY(retryMediaButton);
    QVERIFY(closeMediaButton);
    QVERIFY(playPauseButton);
    QVERIFY(playPauseButtonIcon);
    QVERIFY(seekingState);
    QVERIFY(seekSlider);
    QVERIFY(positionLabel);
    QVERIFY(durationLabel);
    QVERIFY(muteButton);
    QVERIFY(volumeSlider);
    QVERIFY(audioDiagnosticsLabel);
    QVERIFY(playbackStateLabel);
    QVERIFY(statisticsMenuItem);
    QVERIFY(moreButton);
    QVERIFY(transportMenu);
    QVERIFY(subtitleMenu);
    QVERIFY(subtitleOffItem);
    QVERIFY(subtitleEnglishItem);
    QVERIFY(hdrLabMenuItem);
    QVERIFY(closeStatisticsButton);
    QVERIFY(statisticsPanel);
    QVERIFY(transportIsland);
    QVERIFY(seekBackwardButton);
    QVERIFY(seekForwardButton);
    QQuickItem *const seekBackwardButtonItem =
        qobject_cast<QQuickItem *>(seekBackwardButton);
    QQuickItem *const playPauseButtonItem =
        qobject_cast<QQuickItem *>(playPauseButton);
    QQuickItem *const seekForwardButtonItem =
        qobject_cast<QQuickItem *>(seekForwardButton);
    QQuickItem *const seekSliderItem =
        qobject_cast<QQuickItem *>(seekSlider);
    QQuickItem *const volumeSliderItem =
        qobject_cast<QQuickItem *>(volumeSlider);
    QQuickItem *const moreButtonItem =
        qobject_cast<QQuickItem *>(moreButton);
    QVERIFY(seekBackwardButtonItem);
    QVERIFY(playPauseButtonItem);
    QVERIFY(seekForwardButtonItem);
    QVERIFY(seekSliderItem);
    QVERIFY(volumeSliderItem);
    QVERIFY(moreButtonItem);
    QObject *const rendererSwitchContent =
        qvariant_cast<QObject *>(
            rendererSwitch->property("contentItem"));
    QVERIFY(rendererSwitchContent);
    QCOMPARE(
        rendererSwitchContent->property("color").value<QColor>(),
        QColor(QStringLiteral("#f2f4f8")));
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
    QTRY_VERIFY(!windowCommands.windowShortcutsBlocked());
    QTRY_VERIFY(!videoViewport.visible());
    QTRY_VERIFY(emptyState->property("visible").toBool());
    QCOMPARE(clientSideWindowChrome->property("contentTop").toReal(), 0.0);

    windowCommands.windowChromeController().setState(true, false, false);
    QVERIFY(clientSideTitleBar->height() > 0.0);
    QTRY_COMPARE(
        clientSideWindowChrome->property("contentTop").toReal(),
        clientSideTitleBar->height());
    QTRY_VERIFY(clientSideTitleBar->isVisible());

    windowCommands.windowChromeController().setState(true, false, true);
    QTRY_COMPARE(
        clientSideWindowChrome->property("contentTop").toReal(),
        clientSideTitleBar->height());

    windowCommands.windowChromeController().setState(true, true, false);
    QTRY_VERIFY(!clientSideTitleBar->isVisible());
    QTRY_COMPARE(
        clientSideWindowChrome->property("contentTop").toReal(),
        0.0);

    windowCommands.windowChromeController().setState(false, false, false);
    QVERIFY(!openingState->property("visible").toBool());
    QVERIFY(!errorState->property("visible").toBool());
    QVERIFY(!seekSlider->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(
        emptyHdrLabButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(
        activeVideoSource.route(),
        ShellTestActiveVideoSource::Route::Diagnostics);
    QVERIFY(QMetaObject::invokeMethod(
        backToPlayerButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(
        activeVideoSource.route(),
        ShellTestActiveVideoSource::Route::Player);

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

    // Audio output can make the session ready before the first video frame.
    // Keep the playback chrome and presentation viewport active so that frame
    // selection can make progress instead of deadlocking behind hasFrame.
    mediaSession.setState(
        ShellTestMediaSession::State::Ready, false);
    QVERIFY(QMetaObject::invokeMethod(
        playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(videoViewport.visible());
    QTRY_VERIFY(waitingForVideoState->property("visible").toBool());
    QTRY_VERIFY(playPauseButton->property("visible").toBool());
    QTRY_VERIFY(seekSlider->property("visible").toBool());
    QTRY_VERIFY(muteButton->property("visible").toBool());
    QTRY_VERIFY(volumeSlider->property("visible").toBool());
    const QRectF viewportWithoutChrome = videoViewport.rect();
    playerPage->setProperty("controlsVisibleByActivity", false);
    QTRY_VERIFY(clientSideTitleBar->opacity() < 0.1);
    windowCommands.windowChromeController().setState(true, false, false);
    QTRY_COMPARE(videoViewport.rect().y(), 0.0);
    QCOMPARE(videoViewport.rect(), viewportWithoutChrome);
    windowCommands.windowChromeController().setState(false, false, false);
    QVERIFY(QMetaObject::invokeMethod(
        playerPage, "revealControls", Qt::DirectConnection));
    windowCommands.reset();
    QTest::mouseDClick(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        QPoint(80, 80));
    QTRY_COMPARE(windowCommands.toggleCount(), 1);
    playerPage->setProperty("controlsVisibleByActivity", false);
    QTRY_VERIFY(transportIsland->opacity() < 0.1);
    const QPoint transportCenter = transportIsland->mapToScene({
        transportIsland->width() * 0.5,
        transportIsland->height() * 0.5,
    }).toPoint();
    QTest::mouseMove(&quickWindow, transportCenter);
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    windowCommands.reset();
    QTest::mouseDClick(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        transportCenter);
    QCOMPARE(windowCommands.toggleCount(), 0);
    const QPoint seekControlPoint = seekSliderItem->mapToScene({
        5.0,
        seekSliderItem->height() * 0.5,
    }).toPoint();
    QTest::mouseDClick(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        seekControlPoint);
    QCOMPARE(windowCommands.toggleCount(), 0);
    const int seekCountAfterDoubleClick = mediaSession.seekCount();
    const QPoint volumeControlPoint = volumeSliderItem->mapToScene({
        volumeSliderItem->width() * mediaSession.volume(),
        volumeSliderItem->height() * 0.5,
    }).toPoint();
    QTest::mouseDClick(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        volumeControlPoint);
    QCOMPARE(windowCommands.toggleCount(), 0);
    const QPoint moreControlPoint = moreButtonItem->mapToScene({
        moreButtonItem->width() * 0.5,
        moreButtonItem->height() * 0.5,
    }).toPoint();
    QTest::mouseDClick(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        moreControlPoint);
    QCOMPARE(windowCommands.toggleCount(), 0);
    if (transportMenu->property("visible").toBool()) {
        QVERIFY(QMetaObject::invokeMethod(
            transportMenu, "close", Qt::DirectConnection));
        QTRY_VERIFY(!transportMenu->property("visible").toBool());
    }
    QVERIFY(!playerPage->property("controlsPinned").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(transportIsland->opacity() < 0.1, 3500);
    QVERIFY(!windowCommands.cursorHidden());
    QTest::mouseMove(&quickWindow, QPoint(40, 40));
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    QTRY_COMPARE(playPauseButtonIcon->property("status").toInt(), 1);
    QCOMPARE(playPauseButtonIcon->property("width").toDouble(), 17.0);
    QCOMPARE(playPauseButtonIcon->property("height").toDouble(), 17.0);
    const auto centerY = [](const QQuickItem &item) {
        return qRound(item.mapToScene({
            item.width() * 0.5,
            item.height() * 0.5,
        }).y());
    };
    QCOMPARE(centerY(*seekBackwardButtonItem),
             centerY(*playPauseButtonItem));
    QCOMPARE(centerY(*seekForwardButtonItem),
             centerY(*playPauseButtonItem));
    QVERIFY(
        playPauseButtonIcon->property("source").toUrl().path().endsWith(
            QStringLiteral("/pause.svg")));
    QVERIFY(!statisticsPanel->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(
        statisticsMenuItem, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(statisticsPanel->property("visible").toBool());
    QTRY_VERIFY(audioDiagnosticsLabel->property("visible").toBool());
    windowCommands.reset();
    const QPoint statisticsCenter =
        qobject_cast<QQuickItem *>(statisticsPanel)->mapToScene({
            statisticsPanel->property("width").toDouble() * 0.5,
            statisticsPanel->property("height").toDouble() * 0.5,
        }).toPoint();
    QTest::mouseDClick(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        statisticsCenter);
    QCOMPARE(windowCommands.toggleCount(), 0);
    QVERIFY(audioDiagnosticsLabel->property("text").toString().contains(
        QStringLiteral("controlled")));
    QTRY_COMPARE(
        volumeSlider->property("value").toDouble(),
        mediaSession.volume());
    QVERIFY(QMetaObject::invokeMethod(
        muteButton, "clicked", Qt::DirectConnection));
    QVERIFY(mediaSession.muted());
    QCOMPARE(
        muteButton->property("text").toString(),
        QStringLiteral("Unmute"));
    QVERIFY(!volumeSlider->property("enabled").toBool());
    mediaSession.setMuted(false);
    mediaSession.setVolume(0.4);
    QTRY_COMPARE(volumeSlider->property("value").toDouble(), 0.4);
    QVERIFY(QMetaObject::invokeMethod(
        playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    const QPointF volumeStart =
        volumeSliderItem->mapToScene({
            volumeSliderItem->width() * 0.4,
            volumeSliderItem->height() * 0.5,
        });
    const QPointF volumeDestination =
        volumeSliderItem->mapToScene({
            volumeSliderItem->width() * 0.8,
            volumeSliderItem->height() * 0.5,
        });
    QTest::mousePress(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        volumeStart.toPoint());
    QTest::mouseMove(
        &quickWindow,
        volumeDestination.toPoint());
    QTest::mouseRelease(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        volumeDestination.toPoint());
    QTRY_VERIFY(mediaSession.volume() > 0.7);
    QTRY_COMPARE(
        volumeSlider->property("value").toDouble(),
        mediaSession.volume());
    QVERIFY(QMetaObject::invokeMethod(
        closeStatisticsButton, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(!statisticsPanel->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(
        transportMenu, "open", Qt::DirectConnection));
    QTRY_VERIFY(transportMenu->property("visible").toBool());
    QTRY_VERIFY(windowCommands.windowShortcutsBlocked());
    QCOMPARE(mediaSession.selectedSubtitleStreamIndex(), -1);
    QVERIFY(QMetaObject::invokeMethod(
        subtitleEnglishItem, "triggered", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.selectedSubtitleStreamIndex(), 2);
    QTRY_VERIFY(subtitleEnglishItem->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(
        subtitleOffItem, "triggered", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.selectedSubtitleStreamIndex(), -1);
    QTRY_VERIFY(subtitleOffItem->property("checked").toBool());
    QTest::keyClick(&quickWindow, Qt::Key_Escape);
    QTRY_VERIFY(!transportMenu->property("visible").toBool());
    QTRY_VERIFY(!windowCommands.windowShortcutsBlocked());

    mediaSession.setPlaybackInterruption(
        ShellTestMediaSession::PlaybackInterruption::Buffering);
    QVERIFY(!mediaSession.playing());
    QVERIFY(mediaSession.playRequested());
    QTRY_VERIFY(
        playbackStateLabel->property("text").toString().contains(
            QStringLiteral("Buffering audio")));
    QVERIFY(QMetaObject::invokeMethod(
        playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(!mediaSession.playRequested());
    QTRY_VERIFY(
        playbackStateLabel->property("text").toString().contains(
            QStringLiteral("Paused")));
    QVERIFY(QMetaObject::invokeMethod(
        playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(mediaSession.playRequested());
    QTRY_VERIFY(
        playbackStateLabel->property("text").toString().contains(
            QStringLiteral("Buffering audio")));
    mediaSession.setPlaybackInterruption(
        ShellTestMediaSession::PlaybackInterruption::None);

    mediaSession.setState(
        ShellTestMediaSession::State::Ready, true);
    QTRY_VERIFY(!waitingForVideoState->property("visible").toBool());
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
    QVERIFY(QMetaObject::invokeMethod(
        playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(videoViewport.visible());
    QTRY_COMPARE(videoViewport.rect().x(), 0.0);
    QTRY_COMPARE(videoViewport.rect().y(), 0.0);
    QTRY_COMPARE(videoViewport.rect().width(), 1100.0);
    QTRY_COMPARE(videoViewport.rect().height(), 760.0);
    playerPage->setProperty("controlsVisibleByActivity", false);
    QTRY_VERIFY(transportIsland->opacity() < 0.1);
    QTRY_VERIFY(windowCommands.cursorHidden());
    QTest::mouseMove(&quickWindow, QPoint(44, 44));
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    QTRY_VERIFY(!windowCommands.cursorHidden());
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
    QCOMPARE(mediaSession.seekCount(), seekCountAfterDoubleClick);
    QVERIFY(QMetaObject::invokeMethod(
        playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
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
    QCOMPARE(mediaSession.seekCount(), seekCountAfterDoubleClick);
    QTRY_VERIFY(
        positionLabel->property("text").toString()
            != QStringLiteral("0:12"));
    QTest::mouseRelease(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        destination.toPoint());
    QTRY_COMPARE(
        mediaSession.seekCount(),
        seekCountAfterDoubleClick + 1);
    QVERIFY(mediaSession.lastSeekMilliseconds() > 30'000);
    QTRY_COMPARE(
        qRound(seekSlider->property("value").toDouble()),
        static_cast<int>(
            mediaSession.lastSeekMilliseconds()));
    const qlonglong seekedPosition =
        mediaSession.lastSeekMilliseconds();
    QVERIFY(QMetaObject::invokeMethod(
        seekBackwardButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(
        mediaSession.lastSeekMilliseconds(),
        std::max<qlonglong>(0, seekedPosition - 10'000));
    QVERIFY(QMetaObject::invokeMethod(
        seekForwardButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(
        mediaSession.lastSeekMilliseconds(),
        std::min<qlonglong>(
            mediaSession.durationMilliseconds(),
            std::max<qlonglong>(0, seekedPosition - 10'000)
                + 10'000));

    mediaSession.setTimeline(5'000, 65'000, true);
    QVERIFY(QMetaObject::invokeMethod(
        seekBackwardButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.lastSeekMilliseconds(), 0);

    mediaSession.setTimeline(62'000, 65'000, true);
    QVERIFY(QMetaObject::invokeMethod(
        seekForwardButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.lastSeekMilliseconds(), 65'000);

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
    QTRY_COMPARE(videoViewport.rect().width(), 900.0);
    QTRY_VERIFY(videoViewport.rect().height() < originalHeight);
    QTRY_COMPARE(videoViewport.rect().height(), 650.0);

    QVERIFY(QMetaObject::invokeMethod(
        closeMediaButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.cancelCount(), 2);
    QTRY_VERIFY(!videoViewport.visible());
    mediaSession.setState(
        ShellTestMediaSession::State::Ready, true);

    QVERIFY(QMetaObject::invokeMethod(
        hdrLabMenuItem, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(
        activeVideoSource.route(),
        ShellTestActiveVideoSource::Route::Diagnostics);
    windowCommands.reset();
    QTest::mouseDClick(
        &quickWindow,
        Qt::LeftButton,
        Qt::NoModifier,
        QPoint(420, 420));
    QCOMPARE(windowCommands.toggleCount(), 0);
    QTRY_COMPARE(videoViewport.rect().x(), 24.0);
    QTRY_COMPARE(videoViewport.rect().y(), 112.0);
    QTRY_COMPARE(videoViewport.rect().width(), 852.0);

    rootItem->setVisible(false);
    QTRY_VERIFY(!videoViewport.visible());
    rootItem->setVisible(true);
    QTRY_VERIFY(videoViewport.visible());

    videoSource.setUseLibplacebo(false);
    QTRY_COMPARE(
        rendererSwitch->property("checked").toBool(),
        false);

    QVERIFY(QMetaObject::invokeMethod(
        backToPlayerButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(
        activeVideoSource.route(),
        ShellTestActiveVideoSource::Route::Player);
}

QTEST_MAIN(AppShellTest)

#include "tst_AppShell.moc"
