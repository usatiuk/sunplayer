#include <algorithm>
#include <memory>

#include <QColor>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryFile>
#include <QTest>
#include <QVariant>

#include "QmlShellTestTypes.h"
#include "app/VideoViewportState.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
QString componentErrorText(QQmlComponent const& component) {
    QStringList messages;
    for (QQmlError const& error : component.errors()) {
        messages.append(error.toString());
    }
    return messages.join(u'\n');
}
} // namespace

class AppShellTest final : public QObject {
    Q_OBJECT

  private slots:
    void publishesActiveViewport();

  public:
    static void initMain() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }
};

void AppShellTest::publishesActiveViewport() {
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SUNPLAYER_QT_QML_IMPORT_PATH));
    QQmlComponent component(&engine);
    component.loadFromModule(QStringLiteral("SunPlayerShellTest"), QStringLiteral("Main"));
    QVERIFY2(!component.isError(), qPrintable(componentErrorText(component)));

    ShellTestPresentationOutputState outputState(nullptr);
    ShellTestWindowCommands windowCommands(nullptr);
    ShellTestPresentationSettings presentationSettings(nullptr);
    ShellTestDiagnosticVideoSource videoSource(nullptr);
    ShellTestMediaSession mediaSession(nullptr);
    ShellTestActiveVideoSource activeVideoSource(nullptr);
    ShellTestSupportController supportController(nullptr);
    VideoViewportState videoViewport(nullptr);
    QVariantMap const initialProperties{
        {
            QStringLiteral("renderDevicePixelRatio"),
            1.0,
        },
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
        {
            QStringLiteral("supportController"),
            QVariant::fromValue(&supportController),
        },
    };

    std::unique_ptr<QObject> object(component.createWithInitialProperties(initialProperties));
    QVERIFY2(object, qPrintable(componentErrorText(component)));
    QQuickItem* const rootItem = qobject_cast<QQuickItem*>(object.get());
    QVERIFY(rootItem);
    QObject* const rendererSwitch = rootItem->findChild<QObject*>(QStringLiteral("videoRendererSwitch"));
    QObject* const hdrLabHeaderPanel = rootItem->findChild<QObject*>(QStringLiteral("hdrLabHeaderPanel"));
    QObject* const hdrLabOutputPanel = rootItem->findChild<QObject*>(QStringLiteral("hdrLabOutputPanel"));
    QQuickItem* const hdrLabDiagnosticScroll =
        rootItem->findChild<QQuickItem*>(QStringLiteral("hdrLabDiagnosticScroll"));
    QQuickItem* const hdrLabReprobeButton = rootItem->findChild<QQuickItem*>(QStringLiteral("hdrLabReprobeButton"));
    QObject* const hdrLabSourcePeakSlider = rootItem->findChild<QObject*>(QStringLiteral("hdrLabSourcePeakSlider"));
    QObject* const hdrLabSourcePeakLabel = rootItem->findChild<QObject*>(QStringLiteral("hdrLabSourcePeakLabel"));
    QObject* const hdrLabFooterPanel = rootItem->findChild<QObject*>(QStringLiteral("hdrLabFooterPanel"));
    QVERIFY(rendererSwitch);
    QVERIFY(hdrLabHeaderPanel);
    QVERIFY(hdrLabOutputPanel);
    QVERIFY(hdrLabDiagnosticScroll);
    QVERIFY(hdrLabReprobeButton);
    QVERIFY(hdrLabSourcePeakSlider);
    QVERIFY(hdrLabSourcePeakLabel);
    QVERIFY(hdrLabFooterPanel);
    QObject* const emptyState = rootItem->findChild<QObject*>(QStringLiteral("emptyState"));
    QObject* const playerPage = rootItem->findChild<QObject*>(QStringLiteral("playerPage"));
    QObject* const openDialog = rootItem->findChild<QObject*>(QStringLiteral("openDialog"));
    QObject* const playbackHoverHandler = rootItem->findChild<QObject*>(QStringLiteral("playbackHoverHandler"));
    QQuickItem* const fullscreenBackgroundMouseArea =
        rootItem->findChild<QQuickItem*>(QStringLiteral("fullscreenBackgroundMouseArea"));
    QObject* const emptyHdrLabButton = rootItem->findChild<QObject*>(QStringLiteral("emptyHdrLabButton"));
    QObject* const backToPlayerButton = rootItem->findChild<QObject*>(QStringLiteral("backToPlayerButton"));
    QQuickItem* const clientSideWindowChrome =
        rootItem->findChild<QQuickItem*>(QStringLiteral("clientSideWindowChrome"));
    QQuickItem* const clientSideTitleBar = rootItem->findChild<QQuickItem*>(QStringLiteral("clientSideTitleBar"));
    QQuickItem* const clientSideWindowOutline =
        rootItem->findChild<QQuickItem*>(QStringLiteral("clientSideWindowOutline"));
    QObject* const openingState = rootItem->findChild<QObject*>(QStringLiteral("openingState"));
    QObject* const waitingForVideoState = rootItem->findChild<QObject*>(QStringLiteral("waitingForVideoState"));
    QObject* const errorState = rootItem->findChild<QObject*>(QStringLiteral("errorState"));
    QObject* const cancelOpenButton = rootItem->findChild<QObject*>(QStringLiteral("cancelOpenButton"));
    QObject* const retryMediaButton = rootItem->findChild<QObject*>(QStringLiteral("retryMediaButton"));
    QObject* const openAnotherMediaErrorButton =
        rootItem->findChild<QObject*>(QStringLiteral("openAnotherMediaErrorButton"));
    QObject* const closeMediaButton = rootItem->findChild<QObject*>(QStringLiteral("closeMediaButton"));
    QObject* const playPauseButton = rootItem->findChild<QObject*>(QStringLiteral("playPauseButton"));
    QObject* const playPauseButtonIcon = rootItem->findChild<QObject*>(QStringLiteral("playPauseButtonIcon"));
    QObject* const seekingState = rootItem->findChild<QObject*>(QStringLiteral("seekingState"));
    QObject* const relativeSeekTimer = rootItem->findChild<QObject*>(QStringLiteral("relativeSeekTimer"));
    QObject* const preparingIndicator = rootItem->findChild<QObject*>(QStringLiteral("preparingIndicator"));
    QObject* const preparingLabel = rootItem->findChild<QObject*>(QStringLiteral("preparingLabel"));
    QObject* const seekingIndicator = rootItem->findChild<QObject*>(QStringLiteral("seekingIndicator"));
    QObject* const seekingLabel = rootItem->findChild<QObject*>(QStringLiteral("seekingLabel"));
    QObject* const bufferingIndicator = rootItem->findChild<QObject*>(QStringLiteral("bufferingIndicator"));
    QObject* const seekSlider = rootItem->findChild<QObject*>(QStringLiteral("seekSlider"));
    QObject* const positionLabel = rootItem->findChild<QObject*>(QStringLiteral("positionLabel"));
    QObject* const durationLabel = rootItem->findChild<QObject*>(QStringLiteral("durationLabel"));
    QObject* const muteButton = rootItem->findChild<QObject*>(QStringLiteral("muteButton"));
    QObject* const volumeSlider = rootItem->findChild<QObject*>(QStringLiteral("volumeSlider"));
    QObject* const audioDiagnosticsLabel = rootItem->findChild<QObject*>(QStringLiteral("audioDiagnosticsLabel"));
    QObject* const playbackStateLabel = rootItem->findChild<QObject*>(QStringLiteral("playbackStateLabel"));
    QObject* const playbackDetailsMenuItem = rootItem->findChild<QObject*>(QStringLiteral("playbackDetailsMenuItem"));
    QObject* const blankOtherDisplaysMenuItem =
        rootItem->findChild<QObject*>(QStringLiteral("blankOtherDisplaysMenuItem"));
    QObject* const selectedVideoDetailsLabel =
        rootItem->findChild<QObject*>(QStringLiteral("selectedVideoDetailsLabel"));
    QObject* const videoSignalDetailsLabel = rootItem->findChild<QObject*>(QStringLiteral("videoSignalDetailsLabel"));
    QObject* const selectedAudioDetailsLabel =
        rootItem->findChild<QObject*>(QStringLiteral("selectedAudioDetailsLabel"));
    QObject* const selectedSubtitleDetailsLabel =
        rootItem->findChild<QObject*>(QStringLiteral("selectedSubtitleDetailsLabel"));
    QObject* const outputDetailsLabel = rootItem->findChild<QObject*>(QStringLiteral("outputDetailsLabel"));
    QObject* const videoPerformanceDetailsLabel =
        rootItem->findChild<QObject*>(QStringLiteral("videoPerformanceDetailsLabel"));
    QObject* const moreButton = rootItem->findChild<QObject*>(QStringLiteral("moreButton"));
    QObject* const transportMenu = rootItem->findChild<QObject*>(QStringLiteral("transportMenu"));
    QObject* const videoTrackMenu = rootItem->findChild<QObject*>(QStringLiteral("videoTrackMenu"));
    QObject* const videoDarkItem = rootItem->findChild<QObject*>(QStringLiteral("videoTrack_0"));
    QObject* const audioTrackMenu = rootItem->findChild<QObject*>(QStringLiteral("audioTrackMenu"));
    QObject* const audioPositiveItem = rootItem->findChild<QObject*>(QStringLiteral("audioTrack_1"));
    QObject* const subtitleMenu = rootItem->findChild<QObject*>(QStringLiteral("subtitleMenu"));
    QObject* const subtitleOffItem = rootItem->findChild<QObject*>(QStringLiteral("subtitleTrack_-1"));
    QObject* const subtitleEnglishItem = rootItem->findChild<QObject*>(QStringLiteral("subtitleTrack_2"));
    QObject* const hdrLabMenuItem = rootItem->findChild<QObject*>(QStringLiteral("hdrLabMenuItem"));
    QObject* const reportBugMenuItem = rootItem->findChild<QObject*>(QStringLiteral("reportBugMenuItem"));
    QObject* const aboutMenuItem = rootItem->findChild<QObject*>(QStringLiteral("aboutMenuItem"));
    QObject* const idleMoreButton = rootItem->findChild<QObject*>(QStringLiteral("idleMoreButton"));
    QObject* const restartMediaErrorButton = rootItem->findChild<QObject*>(QStringLiteral("restartMediaErrorButton"));
    QObject* const reportMediaErrorButton = rootItem->findChild<QObject*>(QStringLiteral("reportMediaErrorButton"));
    QObject* const quitMediaErrorButton = rootItem->findChild<QObject*>(QStringLiteral("quitMediaErrorButton"));
    QObject* const closePlaybackDetailsButton =
        rootItem->findChild<QObject*>(QStringLiteral("closePlaybackDetailsButton"));
    QObject* const playbackDetailsPanel = rootItem->findChild<QObject*>(QStringLiteral("playbackDetailsPanel"));
    QQuickItem* const transportIsland = rootItem->findChild<QQuickItem*>(QStringLiteral("transportIsland"));
    QObject* const seekBackwardButton = rootItem->findChild<QObject*>(QStringLiteral("seekBackwardButton"));
    QObject* const seekForwardButton = rootItem->findChild<QObject*>(QStringLiteral("seekForwardButton"));
    QVERIFY(emptyState);
    QVERIFY(playerPage);
    QVERIFY(openDialog);
#ifdef Q_OS_MACOS
    QCOMPARE(openDialog->property("parentWindow").value<QWindow*>(), &windowCommands);
#else
    QVERIFY(!openDialog->property("parentWindow").value<QWindow*>());
#endif
    QVERIFY(playbackHoverHandler);
    QVERIFY(fullscreenBackgroundMouseArea);
    QVERIFY(emptyHdrLabButton);
    QVERIFY(backToPlayerButton);
    QVERIFY(clientSideWindowChrome);
    QVERIFY(clientSideTitleBar);
    QVERIFY(clientSideWindowOutline);
    QQmlProperty const outlineBorderWidth(clientSideWindowOutline, QStringLiteral("border.width"));
    QVERIFY(outlineBorderWidth.isValid());
    QVERIFY(openingState);
    QVERIFY(waitingForVideoState);
    QVERIFY(errorState);
    QVERIFY(cancelOpenButton);
    QVERIFY(retryMediaButton);
    QVERIFY(openAnotherMediaErrorButton);
    QVERIFY(closeMediaButton);
    QVERIFY(playPauseButton);
    QVERIFY(playPauseButtonIcon);
    QVERIFY(seekingState);
    QVERIFY(relativeSeekTimer);
    QVERIFY(preparingIndicator);
    QVERIFY(preparingLabel);
    QVERIFY(seekingIndicator);
    QVERIFY(seekingLabel);
    QVERIFY(bufferingIndicator);
    QVERIFY(seekSlider);
    QVERIFY(positionLabel);
    QVERIFY(durationLabel);
    QVERIFY(muteButton);
    QVERIFY(volumeSlider);
    QVERIFY(audioDiagnosticsLabel);
    QVERIFY(playbackStateLabel);
    QVERIFY(playbackDetailsMenuItem);
    QVERIFY(blankOtherDisplaysMenuItem);
    QVERIFY(selectedVideoDetailsLabel);
    QVERIFY(videoSignalDetailsLabel);
    QVERIFY(selectedAudioDetailsLabel);
    QVERIFY(selectedSubtitleDetailsLabel);
    QVERIFY(outputDetailsLabel);
    QVERIFY(videoPerformanceDetailsLabel);
    QVERIFY(moreButton);
    QVERIFY(transportMenu);
    QVERIFY(videoTrackMenu);
    QVERIFY(videoDarkItem);
    QVERIFY(audioTrackMenu);
    QVERIFY(audioPositiveItem);
    QVERIFY(subtitleMenu);
    QVERIFY(subtitleOffItem);
    QVERIFY(subtitleEnglishItem);
    QVERIFY(hdrLabMenuItem);
    QVERIFY(reportBugMenuItem);
    QVERIFY(aboutMenuItem);
    QVERIFY(idleMoreButton);
    QVERIFY(restartMediaErrorButton);
    QVERIFY(reportMediaErrorButton);
    QVERIFY(quitMediaErrorButton);
    QVERIFY(closePlaybackDetailsButton);
    QVERIFY(playbackDetailsPanel);
    QVERIFY(transportIsland);
    QVERIFY(seekBackwardButton);
    QVERIFY(seekForwardButton);
    QQuickItem* const seekBackwardButtonItem = qobject_cast<QQuickItem*>(seekBackwardButton);
    QQuickItem* const playPauseButtonItem = qobject_cast<QQuickItem*>(playPauseButton);
    QQuickItem* const seekForwardButtonItem = qobject_cast<QQuickItem*>(seekForwardButton);
    QQuickItem* const seekSliderItem = qobject_cast<QQuickItem*>(seekSlider);
    QQuickItem* const volumeSliderItem = qobject_cast<QQuickItem*>(volumeSlider);
    QQuickItem* const moreButtonItem = qobject_cast<QQuickItem*>(moreButton);
    QVERIFY(seekBackwardButtonItem);
    QVERIFY(playPauseButtonItem);
    QVERIFY(seekForwardButtonItem);
    QVERIFY(seekSliderItem);
    QVERIFY(volumeSliderItem);
    QVERIFY(moreButtonItem);
    QObject* const rendererSwitchContent = qvariant_cast<QObject*>(rendererSwitch->property("contentItem"));
    QVERIFY(rendererSwitchContent);
    QCOMPARE(rendererSwitchContent->property("color").value<QColor>(), QColor(QStringLiteral("#f2f4f8")));
    QCOMPARE(rendererSwitch->property("checked").toBool(), true);
    QCOMPARE(hdrLabHeaderPanel->property("color").value<QColor>(), QColor(Qt::black));
    QCOMPARE(hdrLabOutputPanel->property("color").value<QColor>(), QColor(Qt::black));
    QCOMPARE(hdrLabFooterPanel->property("color").value<QColor>(), QColor(Qt::black));
    QCOMPARE(videoSource.property("sourcePeakHeadroom").toReal(), 2.0);
    QCOMPARE(hdrLabSourcePeakSlider->property("to").toReal(), 10.0);
    QVERIFY(hdrLabSourcePeakLabel->property("text").toString().contains(QStringLiteral("2.0× 203-nit")));
    QCOMPARE(activeVideoSource.route(), ShellTestActiveVideoSource::Route::Player);

    QQuickWindow quickWindow;
    rootItem->setParentItem(quickWindow.contentItem());
    rootItem->setSize({1100.0, 760.0});
    quickWindow.resize(1100, 760);
    quickWindow.show();
    QTRY_VERIFY(quickWindow.isExposed());
    QTRY_VERIFY(!windowCommands.windowShortcutsBlocked());
    QTRY_VERIFY(!videoViewport.visible());
    QTRY_VERIFY(emptyState->property("visible").toBool());
    QTRY_VERIFY(idleMoreButton->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(idleMoreButton, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(transportMenu->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(reportBugMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(supportController.reportCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(aboutMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(supportController.aboutCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(transportMenu, "close", Qt::DirectConnection));
    QCOMPARE(clientSideWindowChrome->property("contentTop").toReal(), 0.0);
    QVERIFY(!clientSideWindowOutline->isVisible());

    QTemporaryFile dialogMediaFile;
    QVERIFY(dialogMediaFile.open());
    QUrl const dialogMediaUrl = QUrl::fromLocalFile(dialogMediaFile.fileName());
    QVERIFY(openDialog->setProperty("selectedFile", dialogMediaUrl));
    QVERIFY(QMetaObject::invokeMethod(openDialog, "accepted", Qt::DirectConnection));
    QCOMPARE(windowCommands.openCount(), 1);
    QCOMPARE(windowCommands.lastOpenedUrl(), dialogMediaUrl);
    QCOMPARE(mediaSession.openCount(), 0);

    windowCommands.windowChromeController().setState(true, false, false);
    QVERIFY(clientSideTitleBar->height() > 0.0);
    QTRY_COMPARE(clientSideWindowChrome->property("contentTop").toReal(), clientSideTitleBar->height());
    QTRY_VERIFY(clientSideTitleBar->isVisible());
    QTRY_VERIFY(clientSideWindowOutline->isVisible());
    QCOMPARE(clientSideWindowOutline->position(), QPointF());
    QCOMPARE(clientSideWindowOutline->size(), clientSideWindowChrome->size());
    QVERIFY(clientSideWindowOutline->z() > clientSideTitleBar->z());
    QCOMPARE(outlineBorderWidth.read().toReal(), 1.0);
    rootItem->setProperty("renderDevicePixelRatio", 2.0);
    QTRY_COMPARE(outlineBorderWidth.read().toReal(), 0.5);

    windowCommands.windowChromeController().setState(true, false, true);
    QTRY_COMPARE(clientSideWindowChrome->property("contentTop").toReal(), clientSideTitleBar->height());

    windowCommands.windowChromeController().setState(true, true, false);
    QTRY_VERIFY(!clientSideTitleBar->isVisible());
    QTRY_VERIFY(!clientSideWindowOutline->isVisible());
    QTRY_COMPARE(clientSideWindowChrome->property("contentTop").toReal(), 0.0);

    windowCommands.windowChromeController().setState(false, false, false);
    QVERIFY(!openingState->property("visible").toBool());
    QVERIFY(!errorState->property("visible").toBool());
    QVERIFY(!seekSlider->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(emptyHdrLabButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(activeVideoSource.route(), ShellTestActiveVideoSource::Route::Diagnostics);
    rootItem->setSize({760.0, 560.0});
    quickWindow.resize(760, 560);
    QQuickItem* const hdrLabOutputPanelItem = qobject_cast<QQuickItem*>(hdrLabOutputPanel);
    QVERIFY(hdrLabOutputPanelItem);
    QTRY_VERIFY(hdrLabOutputPanelItem->x() >= 0.0);
    QTRY_VERIFY(hdrLabOutputPanelItem->y() >= 0.0);
    QTRY_VERIFY(hdrLabOutputPanelItem->x() + hdrLabOutputPanelItem->width() <= rootItem->width());
    QTRY_VERIFY(hdrLabOutputPanelItem->y() + hdrLabOutputPanelItem->height() <= rootItem->height());
    rootItem->setSize({760.0, 360.0});
    quickWindow.resize(760, 360);
    QTRY_VERIFY(hdrLabDiagnosticScroll->property("contentHeight").toReal() > hdrLabDiagnosticScroll->height());
    QTRY_VERIFY(([&] {
        qreal const diagnosticMaximumY = hdrLabDiagnosticScroll->property("originY").toReal() +
                                         hdrLabDiagnosticScroll->property("contentHeight").toReal() -
                                         hdrLabDiagnosticScroll->height();
        if (!hdrLabDiagnosticScroll->setProperty("contentY", diagnosticMaximumY)) {
            return false;
        }

        QRectF const reprobeBounds =
            hdrLabReprobeButton->mapRectToItem(hdrLabDiagnosticScroll, hdrLabReprobeButton->boundingRect());
        return reprobeBounds.top() >= 0.0 && reprobeBounds.bottom() <= hdrLabDiagnosticScroll->height();
    })());
    rootItem->setSize({1100.0, 760.0});
    quickWindow.resize(1100, 760);
    QVERIFY(QMetaObject::invokeMethod(backToPlayerButton, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(activeVideoSource.route(), ShellTestActiveVideoSource::Route::Player);

    mediaSession.setState(ShellTestMediaSession::State::Opening);
    QTRY_VERIFY(openingState->property("visible").toBool());
    QVERIFY(!videoViewport.visible());
    QVERIFY(QMetaObject::invokeMethod(cancelOpenButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.cancelCount(), 1);
    QCOMPARE(mediaSession.state(), ShellTestMediaSession::State::Empty);

    // Audio output can make the session ready before the first video frame.
    // Keep the playback chrome and presentation viewport active so that frame
    // selection can make progress instead of deadlocking behind hasFrame.
    mediaSession.setState(ShellTestMediaSession::State::Ready, false);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(videoViewport.visible());
    QTRY_VERIFY(!idleMoreButton->property("visible").toBool());
    QTRY_VERIFY(waitingForVideoState->property("visible").toBool());
    QCOMPARE(preparingIndicator->property("width").toInt(), 32);
    QCOMPARE(preparingIndicator->property("height").toInt(), 32);
    QCOMPARE(QQmlProperty(preparingLabel, QStringLiteral("font.pixelSize")).read().toInt(), 14);
    QTRY_VERIFY(playPauseButton->property("visible").toBool());
    QTRY_VERIFY(seekSlider->property("visible").toBool());
    QTRY_VERIFY(muteButton->property("visible").toBool());
    QTRY_VERIFY(volumeSlider->property("visible").toBool());
    QRectF const viewportWithoutChrome = videoViewport.rect();
    playerPage->setProperty("controlsVisibleByActivity", false);
    QTRY_VERIFY(clientSideTitleBar->opacity() < 0.1);
    windowCommands.windowChromeController().setState(true, false, false);
    QTRY_VERIFY(clientSideWindowOutline->isVisible());
    QTRY_COMPARE(videoViewport.rect().y(), 0.0);
    QCOMPARE(videoViewport.rect(), viewportWithoutChrome);
    windowCommands.windowChromeController().setState(false, false, false);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "revealControls", Qt::DirectConnection));
    windowCommands.resetToggleCount();
    constexpr int doubleClickEventDelayMilliseconds = 10;
    QPoint const fullscreenGesturePoint(80, 80);
    QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, fullscreenGesturePoint,
                      doubleClickEventDelayMilliseconds);
    QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, fullscreenGesturePoint,
                        doubleClickEventDelayMilliseconds);
    QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, fullscreenGesturePoint,
                      doubleClickEventDelayMilliseconds);
    QCOMPARE(windowCommands.toggleCount(), 0);
    QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, fullscreenGesturePoint,
                        doubleClickEventDelayMilliseconds);
    QTRY_COMPARE(windowCommands.toggleCount(), 1);

    windowCommands.resetToggleCount();
    QPoint const canceledFullscreenGesturePoint(140, 80);
    QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, canceledFullscreenGesturePoint,
                      doubleClickEventDelayMilliseconds);
    QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, canceledFullscreenGesturePoint,
                        doubleClickEventDelayMilliseconds);
    QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, canceledFullscreenGesturePoint,
                      doubleClickEventDelayMilliseconds);
    fullscreenBackgroundMouseArea->ungrabMouse();
    QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, canceledFullscreenGesturePoint,
                        doubleClickEventDelayMilliseconds);
    QTest::mouseClick(&quickWindow, Qt::LeftButton, Qt::NoModifier, QPoint(220, 80));
    QCOMPARE(windowCommands.toggleCount(), 0);

    playerPage->setProperty("controlsVisibleByActivity", false);
    QTRY_VERIFY(transportIsland->opacity() < 0.1);
    QPoint const transportCenter = transportIsland
                                       ->mapToScene({
                                           transportIsland->width() * 0.5,
                                           transportIsland->height() * 0.5,
                                       })
                                       .toPoint();
    QTest::mouseMove(&quickWindow, transportCenter);
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    windowCommands.resetToggleCount();
    QTest::mouseDClick(&quickWindow, Qt::LeftButton, Qt::NoModifier, transportCenter);
    QCOMPARE(windowCommands.toggleCount(), 0);
    QPoint const seekControlPoint = seekSliderItem
                                        ->mapToScene({
                                            5.0,
                                            seekSliderItem->height() * 0.5,
                                        })
                                        .toPoint();
    QTest::mouseDClick(&quickWindow, Qt::LeftButton, Qt::NoModifier, seekControlPoint);
    QCOMPARE(windowCommands.toggleCount(), 0);
    int const seekCountAfterDoubleClick = mediaSession.seekCount();
    QPoint const volumeControlPoint = volumeSliderItem
                                          ->mapToScene({
                                              volumeSliderItem->width() * mediaSession.volume(),
                                              volumeSliderItem->height() * 0.5,
                                          })
                                          .toPoint();
    QTest::mouseDClick(&quickWindow, Qt::LeftButton, Qt::NoModifier, volumeControlPoint);
    QCOMPARE(windowCommands.toggleCount(), 0);
    QPoint const moreControlPoint = moreButtonItem
                                        ->mapToScene({
                                            moreButtonItem->width() * 0.5,
                                            moreButtonItem->height() * 0.5,
                                        })
                                        .toPoint();
    QTest::mouseDClick(&quickWindow, Qt::LeftButton, Qt::NoModifier, moreControlPoint);
    QCOMPARE(windowCommands.toggleCount(), 0);
    if (transportMenu->property("visible").toBool()) {
        QVERIFY(QMetaObject::invokeMethod(transportMenu, "close", Qt::DirectConnection));
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
    auto const centerY = [](QQuickItem const& item) {
        return qRound(item.mapToScene({
                                          item.width() * 0.5,
                                          item.height() * 0.5,
                                      })
                          .y());
    };
    QCOMPARE(centerY(*seekBackwardButtonItem), centerY(*playPauseButtonItem));
    QCOMPARE(centerY(*seekForwardButtonItem), centerY(*playPauseButtonItem));
    QVERIFY(playPauseButtonIcon->property("source").toUrl().path().endsWith(QStringLiteral("/pause.svg")));
    QVERIFY(!playbackDetailsPanel->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(playbackDetailsMenuItem, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(playbackDetailsPanel->property("visible").toBool());
    QTRY_VERIFY(audioDiagnosticsLabel->property("visible").toBool());
    QVERIFY(selectedVideoDetailsLabel->property("text").toString().contains(QStringLiteral("Czech - Light")));
    QVERIFY(videoSignalDetailsLabel->property("text").toString().contains(QStringLiteral("BT.709 primaries")));
    QVERIFY(selectedAudioDetailsLabel->property("text").toString().contains(QStringLiteral("48 kHz")));
    QCOMPARE(selectedSubtitleDetailsLabel->property("text").toString(), QStringLiteral("Off"));
    QVERIFY(outputDetailsLabel->property("text").toString().contains(QStringLiteral("SDR presentation")));
    QVERIFY(outputDetailsLabel->property("text").toString().contains(QStringLiteral("Test swapchain")));
    QVERIFY(videoPerformanceDetailsLabel->property("text").toString().contains(QStringLiteral("3 decoded")));
    auto const presentationSummary = [&] {
        QVariant result;
        return QMetaObject::invokeMethod(playerPage, "presentationSummary", Qt::DirectConnection,
                                         Q_RETURN_ARG(QVariant, result))
                   ? result.toString()
                   : QString{};
    };
    mediaSession.setVideoHdr(true);
    QCOMPARE(presentationSummary(), QStringLiteral("HDR source mapped to SDR presentation · Test swapchain"));
    outputState.setPresentation(QStringLiteral("HDR10 / BT.2020 PQ"), true, true);
    QCOMPARE(presentationSummary(), QStringLiteral("HDR presentation active · HDR10 / BT.2020 PQ"));
    outputState.setPresentation(QStringLiteral("scRGB / extended linear sRGB"), false, true);
    QCOMPARE(presentationSummary(),
             QStringLiteral("Extended-range presentation active · scRGB / extended linear sRGB"));
    outputState.setPresentation(QStringLiteral("Unavailable"), false, false);
    QCOMPARE(presentationSummary(), QStringLiteral("Presentation details unavailable"));
    mediaSession.setVideoHdr(false);
    outputState.setPresentation(QStringLiteral("Test swapchain"), false, false);
    windowCommands.resetToggleCount();
    QPoint const detailsCenter = qobject_cast<QQuickItem*>(playbackDetailsPanel)
                                     ->mapToScene({
                                         playbackDetailsPanel->property("width").toDouble() * 0.5,
                                         playbackDetailsPanel->property("height").toDouble() * 0.5,
                                     })
                                     .toPoint();
    QTest::mouseDClick(&quickWindow, Qt::LeftButton, Qt::NoModifier, detailsCenter);
    QCOMPARE(windowCommands.toggleCount(), 0);
    QVERIFY(audioDiagnosticsLabel->property("text").toString().contains(QStringLiteral("controlled")));
    QTRY_COMPARE(volumeSlider->property("value").toDouble(), mediaSession.volume());
    QVERIFY(QMetaObject::invokeMethod(muteButton, "clicked", Qt::DirectConnection));
    QVERIFY(mediaSession.muted());
    QCOMPARE(muteButton->property("text").toString(), QStringLiteral("Unmute"));
    QVERIFY(!volumeSlider->property("enabled").toBool());
    mediaSession.setMuted(false);
    mediaSession.setVolume(0.4);
    QTRY_COMPARE(volumeSlider->property("value").toDouble(), 0.4);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    QPointF const volumeStart = volumeSliderItem->mapToScene({
        volumeSliderItem->width() * 0.4,
        volumeSliderItem->height() * 0.5,
    });
    QPointF const volumeDestination = volumeSliderItem->mapToScene({
        volumeSliderItem->width() * 0.8,
        volumeSliderItem->height() * 0.5,
    });
    QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, volumeStart.toPoint());
    QTest::mouseMove(&quickWindow, volumeDestination.toPoint());
    QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, volumeDestination.toPoint());
    QTRY_VERIFY(mediaSession.volume() > 0.7);
    QTRY_COMPARE(volumeSlider->property("value").toDouble(), mediaSession.volume());
    QVERIFY(QMetaObject::invokeMethod(closePlaybackDetailsButton, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(!playbackDetailsPanel->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(transportMenu, "open", Qt::DirectConnection));
    QTRY_VERIFY(transportMenu->property("visible").toBool());
    QTRY_VERIFY(windowCommands.windowShortcutsBlocked());
    windowCommands.setOtherDisplayBlankingAvailable(false);
    QTRY_VERIFY(!blankOtherDisplaysMenuItem->property("visible").toBool());
    windowCommands.setOtherDisplayBlankingAvailable(true);
    QTRY_VERIFY(blankOtherDisplaysMenuItem->property("visible").toBool());
    QVERIFY(!blankOtherDisplaysMenuItem->property("checked").toBool());
    QVERIFY(!windowCommands.blankOtherDisplaysInFullscreen());
    windowCommands.setBlankOtherDisplaysInFullscreen(true);
    QTRY_VERIFY(blankOtherDisplaysMenuItem->property("checked").toBool());
    windowCommands.setBlankOtherDisplaysInFullscreen(false);
    QTRY_VERIFY(!blankOtherDisplaysMenuItem->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(blankOtherDisplaysMenuItem, "clicked", Qt::DirectConnection));
    QVERIFY(windowCommands.blankOtherDisplaysInFullscreen());
    QTRY_VERIFY(blankOtherDisplaysMenuItem->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(blankOtherDisplaysMenuItem, "clicked", Qt::DirectConnection));
    QVERIFY(!windowCommands.blankOtherDisplaysInFullscreen());
    QTRY_VERIFY(!blankOtherDisplaysMenuItem->property("checked").toBool());
    QCOMPARE(mediaSession.selectedVideoStreamIndex(), 2);
    QVERIFY(QMetaObject::invokeMethod(videoDarkItem, "triggered", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.selectedVideoStreamIndex(), 0);
    QTRY_VERIFY(videoDarkItem->property("checked").toBool());
    QTRY_VERIFY(selectedVideoDetailsLabel->property("text").toString().contains(QStringLiteral("English - Dark")));
    QCOMPARE(mediaSession.selectedAudioStreamIndex(), 3);
    QVERIFY(QMetaObject::invokeMethod(audioPositiveItem, "triggered", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.selectedAudioStreamIndex(), 1);
    QTRY_VERIFY(audioPositiveItem->property("checked").toBool());
    QTRY_VERIFY(selectedAudioDetailsLabel->property("text").toString().contains(QStringLiteral("English - Positive")));
    QCOMPARE(mediaSession.selectedSubtitleStreamIndex(), -1);
    QVERIFY(QMetaObject::invokeMethod(subtitleEnglishItem, "triggered", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.selectedSubtitleStreamIndex(), 2);
    QTRY_VERIFY(subtitleEnglishItem->property("checked").toBool());
    QTRY_VERIFY(selectedSubtitleDetailsLabel->property("text").toString().contains(QStringLiteral("ass · text")));
    QVERIFY(QMetaObject::invokeMethod(subtitleOffItem, "triggered", Qt::DirectConnection));
    QTRY_COMPARE(mediaSession.selectedSubtitleStreamIndex(), -1);
    QTRY_VERIFY(subtitleOffItem->property("checked").toBool());
    QTRY_COMPARE(selectedSubtitleDetailsLabel->property("text").toString(), QStringLiteral("Off"));
    QTest::keyClick(&quickWindow, Qt::Key_Escape);
    QTRY_VERIFY(!transportMenu->property("visible").toBool());
    QTRY_VERIFY(!windowCommands.windowShortcutsBlocked());

    QVERIFY(QMetaObject::invokeMethod(reportBugMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(supportController.reportCount(), 2);
    QVERIFY(QMetaObject::invokeMethod(aboutMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(supportController.aboutCount(), 2);

    mediaSession.setState(ShellTestMediaSession::State::Ready, true);
    playerPage->setProperty("controlsVisibleByActivity", false);
    QTRY_VERIFY(transportIsland->opacity() < 0.1);
    mediaSession.setPlaybackInterruption(ShellTestMediaSession::PlaybackInterruption::Buffering);
    QVERIFY(!mediaSession.playing());
    QVERIFY(mediaSession.playRequested());
    QTRY_VERIFY(bufferingIndicator->property("visible").toBool());
    QTRY_VERIFY(transportIsland->opacity() < 0.1);
    QTRY_VERIFY(playbackStateLabel->property("text").toString().contains(QStringLiteral("Buffering audio")));
    QVERIFY(QMetaObject::invokeMethod(playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(!mediaSession.playRequested());
    QTRY_VERIFY(!bufferingIndicator->property("visible").toBool());
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    QTRY_VERIFY(playbackStateLabel->property("text").toString().contains(QStringLiteral("Paused")));
    QVERIFY(QMetaObject::invokeMethod(playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(mediaSession.playRequested());
    QTRY_VERIFY(bufferingIndicator->property("visible").toBool());
    QTRY_VERIFY(playbackStateLabel->property("text").toString().contains(QStringLiteral("Buffering audio")));
    mediaSession.setPlaybackInterruption(ShellTestMediaSession::PlaybackInterruption::None);
    QTRY_VERIFY(!bufferingIndicator->property("visible").toBool());

    mediaSession.setState(ShellTestMediaSession::State::Ready, true);
    QTRY_VERIFY(!waitingForVideoState->property("visible").toBool());
    mediaSession.setState(ShellTestMediaSession::State::Error);
    QTRY_VERIFY(errorState->property("visible").toBool());
    QVERIFY(retryMediaButton->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(retryMediaButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.retryCount(), 1);
    QCOMPARE(mediaSession.openCount(), 0);
    QVERIFY(QMetaObject::invokeMethod(openAnotherMediaErrorButton, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(openDialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(openDialog, "close", Qt::DirectConnection));
    QTRY_VERIFY(!openDialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(restartMediaErrorButton, "clicked", Qt::DirectConnection));
    QCOMPARE(windowCommands.restartCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(reportMediaErrorButton, "clicked", Qt::DirectConnection));
    QCOMPARE(supportController.reportCount(), 3);
    QVERIFY(QMetaObject::invokeMethod(quitMediaErrorButton, "clicked", Qt::DirectConnection));
    QCOMPARE(windowCommands.quitCount(), 1);

    mediaSession.setState(ShellTestMediaSession::State::Ready, true);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "revealControls", Qt::DirectConnection));
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
    QCOMPARE(durationLabel->property("text").toString(), QStringLiteral("1:05"));

    mediaSession.setTimeline(12'500, 65'000, true);
    QTRY_COMPARE(seekSlider->property("value").toDouble(), 12'500.0);
    QCOMPARE(positionLabel->property("text").toString(), QStringLiteral("0:12"));
    QCOMPARE(mediaSession.seekCount(), seekCountAfterDoubleClick);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(transportIsland->opacity() > 0.9);
    QPointF const start = seekSliderItem->mapToScene({
        seekSliderItem->width() * 0.2,
        seekSliderItem->height() * 0.5,
    });
    QPointF const destination = seekSliderItem->mapToScene({
        seekSliderItem->width() * 0.7,
        seekSliderItem->height() * 0.5,
    });
    QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, start.toPoint());
    QTest::mouseMove(&quickWindow, destination.toPoint());
    QCOMPARE(mediaSession.seekCount(), seekCountAfterDoubleClick);
    QTRY_VERIFY(positionLabel->property("text").toString() != QStringLiteral("0:12"));
    QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, destination.toPoint());
    QTRY_COMPARE(mediaSession.seekCount(), seekCountAfterDoubleClick + 1);
    QVERIFY(mediaSession.lastSeekMilliseconds() > 30'000);
    QTRY_COMPARE(qRound(seekSlider->property("value").toDouble()),
                 static_cast<int>(mediaSession.lastSeekMilliseconds()));
    QCOMPARE(relativeSeekTimer->property("interval").toInt(), 180);
    int const seekCountAfterSlider = mediaSession.seekCount();

    // Relative commands share one fixed origin and dispatch once after the
    // burst. The production flush function keeps the test deterministic.
    mediaSession.setTimeline(30'000, 65'000, true);
    emit windowCommands.relativeSeekRequested(10'000);
    emit windowCommands.relativeSeekRequested(10'000);
    emit windowCommands.relativeSeekRequested(10'000);
    QVERIFY(QMetaObject::invokeMethod(relativeSeekTimer, "stop", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountAfterSlider);
    QVERIFY(playerPage->property("relativeSeekPending").toBool());
    QCOMPARE(qRound(playerPage->property("pendingRelativeSeekTargetMilliseconds").toDouble()), 60'000);
    QTRY_COMPARE(positionLabel->property("text").toString(), QStringLiteral("1:00"));
    QTRY_VERIFY(seekingState->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountAfterSlider + 1);
    QCOMPARE(mediaSession.lastSeekMilliseconds(), 60'000);
    QVERIFY(!playerPage->property("relativeSeekPending").toBool());

    // Equal opposite commands cancel the burst instead of drifting because
    // clamping is applied only to the displayed/dispatched target.
    mediaSession.setTimeline(30'000, 65'000, true);
    int const seekCountBeforeCancellation = mediaSession.seekCount();
    emit windowCommands.relativeSeekRequested(10'000);
    emit windowCommands.relativeSeekRequested(-10'000);
    QVERIFY(!playerPage->property("relativeSeekPending").toBool());
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeCancellation);

    // Reversal after hitting a boundary must also cancel from the fixed
    // origin; sequentially clamping each tap would incorrectly drift to 10s.
    mediaSession.setTimeline(5'000, 65'000, true);
    emit windowCommands.relativeSeekRequested(-10'000);
    emit windowCommands.relativeSeekRequested(10'000);
    QVERIFY(!playerPage->property("relativeSeekPending").toBool());
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeCancellation);

    mediaSession.setTimeline(5'000, 65'000, true);
    QVERIFY(QMetaObject::invokeMethod(seekBackwardButton, "clicked", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(relativeSeekTimer, "stop", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeCancellation);
    QCOMPARE(qRound(playerPage->property("pendingRelativeSeekTargetMilliseconds").toDouble()), 0);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.lastSeekMilliseconds(), 0);

    mediaSession.setTimeline(62'000, 65'000, true);
    QVERIFY(QMetaObject::invokeMethod(seekForwardButton, "clicked", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(relativeSeekTimer, "stop", Qt::DirectConnection));
    QCOMPARE(qRound(playerPage->property("pendingRelativeSeekTargetMilliseconds").toDouble()), 65'000);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.lastSeekMilliseconds(), 65'000);

    mediaSession.setTimeline(42'000, 65'000, false);
    QTRY_VERIFY(!seekSlider->property("enabled").toBool());
    mediaSession.setTimeline(42'000, 65'000, true);
    QTRY_VERIFY(seekSlider->property("enabled").toBool());

    // A slow seek keeps the old frame and controls visible. New relative
    // commands still form a single latest-wins replacement request.
    mediaSession.setState(ShellTestMediaSession::State::Opening, true);
    mediaSession.setTimeline(42'000, 65'000, true, true);
    QTRY_VERIFY(seekingState->property("visible").toBool());
    QVERIFY(!openingState->property("visible").toBool());
    QVERIFY(seekSlider->property("enabled").toBool());
    QVERIFY(seekBackwardButton->property("enabled").toBool());
    QVERIFY(seekForwardButton->property("enabled").toBool());
    QVERIFY(playPauseButton->property("enabled").toBool());
    QVERIFY(videoViewport.visible());
    QTRY_COMPARE(seekingIndicator->property("width").toInt(), preparingIndicator->property("width").toInt());
    QTRY_COMPARE(seekingIndicator->property("height").toInt(), preparingIndicator->property("height").toInt());
    QCOMPARE(QQmlProperty(seekingLabel, QStringLiteral("font.pixelSize")).read().toInt(),
             QQmlProperty(preparingLabel, QStringLiteral("font.pixelSize")).read().toInt());

    // Pressing the absolute scrubber cancels an undelivered relative burst,
    // so it cannot launch an obsolete seek while the drag is held.
    int const seekCountBeforeAbsoluteSupersession = mediaSession.seekCount();
    emit windowCommands.relativeSeekRequested(10'000);
    QVERIFY(playerPage->property("relativeSeekPending").toBool());
    QPointF const activeSeekSliderStart = seekSliderItem->mapToScene({
        seekSliderItem->width() * 0.55,
        seekSliderItem->height() * 0.5,
    });
    QPointF const activeSeekSliderDestination = seekSliderItem->mapToScene({
        seekSliderItem->width() * 0.8,
        seekSliderItem->height() * 0.5,
    });
    QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, activeSeekSliderStart.toPoint());
    QTRY_VERIFY(!playerPage->property("relativeSeekPending").toBool());
    QTest::qWait(220);
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeAbsoluteSupersession);
    QTest::mouseMove(&quickWindow, activeSeekSliderDestination.toPoint());
    QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, activeSeekSliderDestination.toPoint());
    QTRY_COMPARE(mediaSession.seekCount(), seekCountBeforeAbsoluteSupersession + 1);
    QVERIFY(mediaSession.lastSeekMilliseconds() > 45'000);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeAbsoluteSupersession + 1);
    mediaSession.setTimeline(42'000, 65'000, true, true);

    QVERIFY(mediaSession.playRequested());
    QVERIFY(QMetaObject::invokeMethod(playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(!mediaSession.playRequested());
    QVERIFY(QMetaObject::invokeMethod(playPauseButton, "clicked", Qt::DirectConnection));
    QVERIFY(mediaSession.playRequested());

    int const seekCountBeforeSupersession = mediaSession.seekCount();
    emit windowCommands.relativeSeekRequested(-10'000);
    emit windowCommands.relativeSeekRequested(-10'000);
    QVERIFY(QMetaObject::invokeMethod(relativeSeekTimer, "stop", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeSupersession);
    QCOMPARE(qRound(playerPage->property("pendingRelativeSeekTargetMilliseconds").toDouble()), 22'000);
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeSupersession + 1);
    QCOMPARE(mediaSession.lastSeekMilliseconds(), 22'000);

    // Leaving the active playback/seek state invalidates an undelivered burst.
    mediaSession.setState(ShellTestMediaSession::State::Ready, true);
    mediaSession.setTimeline(22'000, 65'000, true);
    emit windowCommands.relativeSeekRequested(10'000);
    QVERIFY(QMetaObject::invokeMethod(relativeSeekTimer, "stop", Qt::DirectConnection));
    QVERIFY(playerPage->property("relativeSeekPending").toBool());
    int const seekCountBeforeInvalidation = mediaSession.seekCount();
    mediaSession.setState(ShellTestMediaSession::State::Opening, true);
    QTRY_VERIFY(!playerPage->property("relativeSeekPending").toBool());
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountBeforeInvalidation);
    mediaSession.setState(ShellTestMediaSession::State::Ready, true);

    qreal const originalHeight = videoViewport.rect().height();
    rootItem->setSize({900.0, 650.0});
    QTRY_COMPARE(videoViewport.rect().width(), 900.0);
    QTRY_VERIFY(videoViewport.rect().height() < originalHeight);
    QTRY_COMPARE(videoViewport.rect().height(), 650.0);

    QVERIFY(QMetaObject::invokeMethod(closeMediaButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.cancelCount(), 2);
    QTRY_VERIFY(!videoViewport.visible());
    mediaSession.setState(ShellTestMediaSession::State::Ready, true);

    QVERIFY(QMetaObject::invokeMethod(hdrLabMenuItem, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(activeVideoSource.route(), ShellTestActiveVideoSource::Route::Diagnostics);
    windowCommands.resetToggleCount();
    QTest::mouseDClick(&quickWindow, Qt::LeftButton, Qt::NoModifier, QPoint(420, 420));
    QCOMPARE(windowCommands.toggleCount(), 0);
    QTRY_COMPARE(videoViewport.rect().x(), 24.0);
    QTRY_COMPARE(videoViewport.rect().y(), 112.0);
    QTRY_COMPARE(videoViewport.rect().width(), 852.0);

    rootItem->setVisible(false);
    QTRY_VERIFY(!videoViewport.visible());
    rootItem->setVisible(true);
    QTRY_VERIFY(videoViewport.visible());

    videoSource.setUseLibplacebo(false);
    QTRY_COMPARE(rendererSwitch->property("checked").toBool(), false);

    QUrl const droppedMediaUrl = QUrl::fromLocalFile(QStringLiteral("C:/Media/dropped film.mkv"));
    windowCommands.openMedia(droppedMediaUrl);
    QTRY_COMPARE(activeVideoSource.route(), ShellTestActiveVideoSource::Route::Player);
    QCOMPARE(windowCommands.openCount(), 2);
    QCOMPARE(windowCommands.lastOpenedUrl(), droppedMediaUrl);
    QCOMPARE(mediaSession.openCount(), 0);
}

QTEST_MAIN(AppShellTest)

#include "tst_AppShell.moc"
