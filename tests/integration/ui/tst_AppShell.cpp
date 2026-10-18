#include <algorithm>
#include <memory>

#include "../jellyfin/HttpEdge.h"
#include "jellyfin/JellyfinBrowser.h"
#include "jellyfin/JellyfinImages.h"
#include <QColor>
#include <QDir>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
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
    void initTestCase() { QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark); }
    void publishesActiveViewport();
    void transportButtonShowsKeyboardFocus();
    void appScrollbarPreservesPlatformPalette();

  public:
    static void initMain() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
            qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
        }
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
    QTemporaryDir jellyfinSettings;
    auto settings = createTemporarySettingsStore(jellyfinSettings.filePath(QStringLiteral("settings.ini")));
    JellyfinBrowser jellyfin(*settings, false);
    QVariantMap const initialProperties{
        {QStringLiteral("jellyfin"), QVariant::fromValue(&jellyfin)},
        {
            QStringLiteral("renderDevicePixelRatio"),
            1.0,
        },
        {
            QStringLiteral("applicationDisplayName"),
            QStringLiteral("SunPlayer (Test)"),
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
    // Native backgrounds and text must retain the style's matching colors.
    // Compare with an unparented control so a shell-wide palette override is caught.
    QQmlComponent stockButtonComponent(&engine);
    stockButtonComponent.setData("import QtQuick.Controls; Button { text: \"Reference\" }", QUrl());
    std::unique_ptr<QObject> stockButton(stockButtonComponent.create());
    QVERIFY2(stockButton, qPrintable(componentErrorText(stockButtonComponent)));
    auto matchesSystemButton = [&](QObject* button) {
        if (!button) {
            return false;
        }
        stockButton->setProperty("enabled", button->property("enabled"));
        return QQmlProperty(button, QStringLiteral("palette.buttonText")).read() ==
                   QQmlProperty(stockButton.get(), QStringLiteral("palette.buttonText")).read() &&
               QQmlProperty(button, QStringLiteral("palette.button")).read() ==
                   QQmlProperty(stockButton.get(), QStringLiteral("palette.button")).read();
    };
    QVERIFY(matchesSystemButton(rootItem->findChild<QObject*>(QStringLiteral("openMediaButton"))));
    QVERIFY(matchesSystemButton(rootItem->findChild<QObject*>(QStringLiteral("openJellyfinButton"))));
    rootItem->setProperty("currentPage", 2);
    QTRY_VERIFY(rootItem->findChild<QObject*>(QStringLiteral("jellyfinServer")));
    QVERIFY(!rootItem->property("activeVideoViewportVisible").toBool());
    QVERIFY(rootItem->findChild<QObject*>(QStringLiteral("jellyfinPassword")));
    QVERIFY(matchesSystemButton(rootItem->findChild<QObject*>(QStringLiteral("jellyfinSignIn"))));
    auto* remember = rootItem->findChild<QObject*>(QStringLiteral("jellyfinRemember"));
    QVERIFY(remember);
    QCOMPARE(remember->property("text").toString(), QStringLiteral("Remember me"));
    auto* rememberText = remember->property("contentItem").value<QQuickItem*>();
    QVERIFY(rememberText);
    QVERIFY(rememberText->property("color").value<QColor>().lightnessF() > 0.8);
    rootItem->setProperty("currentPage", 0);
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
    QObject* const openMediaButton = rootItem->findChild<QObject*>(QStringLiteral("openMediaButton"));
    QObject* const openDialog = rootItem->findChild<QObject*>(QStringLiteral("openDialog"));
    QObject* const windowsAppIsolationWarning =
        rootItem->findChild<QObject*>(QStringLiteral("windowsAppIsolationWarning"));
    QObject* const playbackHoverHandler = rootItem->findChild<QObject*>(QStringLiteral("playbackHoverHandler"));
    QQuickItem* const fullscreenBackgroundMouseArea =
        rootItem->findChild<QQuickItem*>(QStringLiteral("fullscreenBackgroundMouseArea"));
    QObject* const backToPlayerButton = rootItem->findChild<QObject*>(QStringLiteral("backToPlayerButton"));
    QQuickItem* const clientSideWindowChrome =
        rootItem->findChild<QQuickItem*>(QStringLiteral("clientSideWindowChrome"));
    QQuickItem* const clientSideTitleBar = rootItem->findChild<QQuickItem*>(QStringLiteral("clientSideTitleBar"));
    QObject* const clientSideApplicationTitle =
        rootItem->findChild<QObject*>(QStringLiteral("clientSideApplicationTitle"));
    QQuickItem* const clientSideWindowOutline =
        rootItem->findChild<QQuickItem*>(QStringLiteral("clientSideWindowOutline"));
    QObject* const openingState = rootItem->findChild<QObject*>(QStringLiteral("openingState"));
    QObject* const playbackActivityState = rootItem->findChild<QObject*>(QStringLiteral("playbackActivityState"));
    QObject* const errorState = rootItem->findChild<QObject*>(QStringLiteral("errorState"));
    QObject* const cancelOpenButton = rootItem->findChild<QObject*>(QStringLiteral("cancelOpenButton"));
    QObject* const retryMediaButton = rootItem->findChild<QObject*>(QStringLiteral("retryMediaButton"));
    QObject* const openAnotherMediaErrorButton =
        rootItem->findChild<QObject*>(QStringLiteral("openAnotherMediaErrorButton"));
    QObject* const closeMediaButton = rootItem->findChild<QObject*>(QStringLiteral("closeMediaButton"));
    QObject* const playPauseButton = rootItem->findChild<QObject*>(QStringLiteral("playPauseButton"));
    QObject* const playPauseButtonIcon = rootItem->findChild<QObject*>(QStringLiteral("playPauseButtonIcon"));
    QObject* const relativeSeekTimer = rootItem->findChild<QObject*>(QStringLiteral("relativeSeekTimer"));
    QObject* const playbackActivityIndicator =
        rootItem->findChild<QObject*>(QStringLiteral("playbackActivityIndicator"));
    QObject* const playbackActivityLabel = rootItem->findChild<QObject*>(QStringLiteral("playbackActivityLabel"));
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
    QObject* const reportBugMenuItem = rootItem->findChild<QObject*>(QStringLiteral("reportBugMenuItem"));
    QObject* const aboutMenuItem = rootItem->findChild<QObject*>(QStringLiteral("aboutMenuItem"));
    QObject* const settingsMenuItem = rootItem->findChild<QObject*>(QStringLiteral("settingsMenuItem"));
    QObject* const subtitleSettingsMenuItem = rootItem->findChild<QObject*>(QStringLiteral("subtitleSettingsMenuItem"));
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
    QVERIFY(openMediaButton);
    QVERIFY(openDialog);
    QVERIFY(windowsAppIsolationWarning);
#ifdef Q_OS_MACOS
    QCOMPARE(openDialog->property("parentWindow").value<QWindow*>(), &windowCommands);
#else
    QVERIFY(!openDialog->property("parentWindow").value<QWindow*>());
#endif
    QVERIFY(playbackHoverHandler);
    QVERIFY(fullscreenBackgroundMouseArea);
    QVERIFY(!rootItem->findChild<QObject*>(QStringLiteral("emptyHdrLabButton")));
    QVERIFY(backToPlayerButton);
    QVERIFY(clientSideWindowChrome);
    QVERIFY(clientSideTitleBar);
    QVERIFY(clientSideApplicationTitle);
    QVERIFY(clientSideWindowOutline);
    QQmlProperty const outlineBorderWidth(clientSideWindowOutline, QStringLiteral("border.width"));
    QVERIFY(outlineBorderWidth.isValid());
    QVERIFY(openingState);
    QVERIFY(playbackActivityState);
    QVERIFY(errorState);
    QVERIFY(cancelOpenButton);
    QVERIFY(retryMediaButton);
    QVERIFY(openAnotherMediaErrorButton);
    QVERIFY(closeMediaButton);
    QVERIFY(playPauseButton);
    QVERIFY(playPauseButtonIcon);
    QVERIFY(relativeSeekTimer);
    QVERIFY(playbackActivityIndicator);
    QVERIFY(playbackActivityLabel);
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
    QVERIFY(!rootItem->findChild<QObject*>(QStringLiteral("hdrLabMenuItem")));
    QVERIFY(reportBugMenuItem);
    QVERIFY(aboutMenuItem);
    QVERIFY(settingsMenuItem);
    QVERIFY(subtitleSettingsMenuItem);
    QCOMPARE(clientSideApplicationTitle->property("text").toString(), QStringLiteral("SunPlayer (Test)"));
    QCOMPARE(aboutMenuItem->property("text").toString(), QStringLiteral("About SunPlayer (Test)"));
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
    for (auto const* name :
         {"toneMapSwitch", "animatePatternSwitch", "automaticTargetPeakSwitch", "videoRendererSwitch"}) {
        auto* const control = rootItem->findChild<QObject*>(QString::fromLatin1(name));
        QVERIFY(control);
        auto* const content = qvariant_cast<QObject*>(control->property("contentItem"));
        QVERIFY(content);
        QCOMPARE(content->property("color").value<QColor>(), QColor(QStringLiteral("#f2f4f8")));
    }
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
    auto* const updateButton = rootItem->findChild<QQuickItem*>(QStringLiteral("applicationUpdateButton"));
    auto* const updateError = rootItem->findChild<QQuickItem*>(QStringLiteral("applicationUpdateError"));
    QVERIFY(updateButton);
    QVERIFY(updateError);
    QVERIFY(!updateButton->isVisible());
    ShellTestApplicationUpdate applicationUpdate;
    windowCommands.setApplicationUpdate(&applicationUpdate);
    QVERIFY(!updateButton->isVisible());
    QVERIFY(applicationUpdate.setProperty("available", true));
    QTRY_VERIFY(updateButton->isVisible());
    QVERIFY(QMetaObject::invokeMethod(updateButton, "clicked", Qt::DirectConnection));
    QCOMPARE(applicationUpdate.updateCount, 1);
    QVERIFY(applicationUpdate.setProperty("updating", true));
    QTRY_VERIFY(!updateButton->isEnabled());
    QVERIFY(updateButton->property("text").toString().contains(QStringLiteral("Updating")));
    QVERIFY(applicationUpdate.setProperty("updating", false));
    QVERIFY(applicationUpdate.setProperty("error", QStringLiteral("Update failed")));
    QTRY_VERIFY(updateError->isVisible());
    QCOMPARE(updateError->property("text").toString(), QStringLiteral("Update failed"));
    QVERIFY(applicationUpdate.setProperty("error", QString{}));
    auto* const dismissUpdateButton =
        rootItem->findChild<QQuickItem*>(QStringLiteral("applicationUpdateDismissButton"));
    QVERIFY(dismissUpdateButton);
    QTRY_VERIFY(dismissUpdateButton->isVisible());
    QCOMPARE(updateButton->property("text").toString(), QStringLiteral("Update and restart"));
    QVERIFY(applicationUpdate.setProperty("updating", true));
    QTRY_VERIFY(!dismissUpdateButton->isEnabled());
    QVERIFY(applicationUpdate.setProperty("updating", false));
    QVERIFY(QMetaObject::invokeMethod(dismissUpdateButton, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(!updateButton->isVisible());
    QVERIFY(!dismissUpdateButton->isVisible());
    QCOMPARE(applicationUpdate.updateCount, 1);
    QVERIFY(applicationUpdate.setProperty("available", true));

    QTRY_VERIFY(idleMoreButton->property("visible").toBool());
    // A light control palette on the dark player canvas must keep both the
    // idle icon and the keyboard-focused button readable.
    auto* const idleButtonItem = qobject_cast<QQuickItem*>(idleMoreButton);
    QVERIFY(idleButtonItem);
    QVERIFY(QQmlProperty(idleMoreButton, QStringLiteral("palette.button")).write(QColor(Qt::white)));
    QVERIFY(QQmlProperty(idleMoreButton, QStringLiteral("palette.buttonText")).write(QColor(Qt::black)));
    QCOMPARE(QQmlProperty(idleMoreButton, QStringLiteral("icon.color")).read().value<QColor>(), QColor(Qt::black));
    auto* const idleBackground = qvariant_cast<QObject*>(idleMoreButton->property("background"));
    QVERIFY(idleBackground);
    QTRY_VERIFY(idleBackground->property("visible").toBool());
    idleButtonItem->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(idleMoreButton->property("visualFocus").toBool());
    QVERIFY(idleBackground->property("visible").toBool());
    QCOMPARE(QQmlProperty(idleMoreButton, QStringLiteral("icon.color")).read().value<QColor>(), QColor(Qt::black));
    rootItem->forceActiveFocus(Qt::OtherFocusReason);
    QVERIFY(!windowsAppIsolationWarning->property("visible").toBool());
    QVERIFY(supportController.setProperty("windowsAppIsolationWarningVisible", true));
    QTRY_VERIFY(windowsAppIsolationWarning->property("visible").toBool());
    QVERIFY(supportController.setProperty("windowsAppIsolationWarningVisible", false));
    QTRY_VERIFY(!windowsAppIsolationWarning->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(idleMoreButton, "clicked", Qt::DirectConnection));
    QTRY_VERIFY(transportMenu->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(reportBugMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(supportController.reportCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(aboutMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(supportController.aboutCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(settingsMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(windowCommands.settingsPage(), 0);
    QVERIFY(QMetaObject::invokeMethod(subtitleSettingsMenuItem, "clicked", Qt::DirectConnection));
    QCOMPARE(windowCommands.settingsPage(), 1);
    QVERIFY(QMetaObject::invokeMethod(transportMenu, "close", Qt::DirectConnection));
    QCOMPARE(clientSideWindowChrome->property("contentTop").toReal(), 0.0);
    QVERIFY(!clientSideWindowOutline->isVisible());

    QVERIFY(QMetaObject::invokeMethod(openMediaButton, "clicked", Qt::DirectConnection));
    QCOMPARE(windowCommands.chooseMediaCount(), 1);
    QTRY_VERIFY(openDialog->property("visible").toBool());

    QTemporaryFile dialogMediaFile;
    QVERIFY(dialogMediaFile.open());
    QUrl const dialogMediaUrl = QUrl::fromLocalFile(dialogMediaFile.fileName());
    QVERIFY(openDialog->setProperty("selectedFile", dialogMediaUrl));
    QVERIFY(QMetaObject::invokeMethod(openDialog, "accepted", Qt::DirectConnection));
    QCOMPARE(windowCommands.openCount(), 1);
    QCOMPARE(windowCommands.lastOpenedUrl(), dialogMediaUrl);
    QCOMPARE(mediaSession.openCount(), 0);
    QVERIFY(QMetaObject::invokeMethod(openDialog, "close", Qt::DirectConnection));
    QTRY_VERIFY(!openDialog->property("visible").toBool());

    windowCommands.windowChromeController().setState(true, false, false);
    QVERIFY(clientSideTitleBar->height() > 0.0);
    QTRY_COMPARE(clientSideWindowChrome->property("contentTop").toReal(), clientSideTitleBar->height());
    QTRY_VERIFY(clientSideTitleBar->isVisible());
    // Every custom titlebar button must reveal keyboard focus without a
    // pointer hover, including the close button's separate background.
    for (auto const* name : {"minimizeWindowButton", "maximizeWindowButton", "closeWindowButton"}) {
        auto* const button = rootItem->findChild<QQuickItem*>(QString::fromLatin1(name));
        QVERIFY(button);
        auto* const background = qvariant_cast<QObject*>(button->property("background"));
        QVERIFY(background);
        button->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(button->property("visualFocus").toBool());
        QVERIFY(background->property("color").value<QColor>().alpha() > 0);
    }
    rootItem->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(clientSideWindowOutline->isVisible());
    QCOMPARE(clientSideWindowOutline->position(), QPointF());
    QCOMPARE(clientSideWindowOutline->size(), clientSideWindowChrome->size());
    QVERIFY(clientSideWindowOutline->z() > clientSideTitleBar->z());
    QCOMPARE(outlineBorderWidth.read().toReal(), 1.0);
    rootItem->setProperty("renderDevicePixelRatio", 2.0);
    QTRY_COMPARE(outlineBorderWidth.read().toReal(), 0.5);

    windowCommands.windowChromeController().setState(true, false, true);
    QTRY_COMPARE(clientSideWindowChrome->property("contentTop").toReal(), clientSideTitleBar->height());

    QTRY_VERIFY(clientSideTitleBar->isVisible());
    QTRY_VERIFY(!clientSideWindowOutline->isVisible());

    windowCommands.windowChromeController().setState(true, false, false, true);
    QTRY_VERIFY(clientSideTitleBar->isVisible());
    QTRY_VERIFY(!clientSideWindowOutline->isVisible());

    windowCommands.windowChromeController().setState(true, false, false);
    QTRY_VERIFY(clientSideWindowOutline->isVisible());

    windowCommands.windowChromeController().setState(true, true, false);
    QTRY_VERIFY(!clientSideTitleBar->isVisible());
    QTRY_VERIFY(!clientSideWindowOutline->isVisible());
    QTRY_COMPARE(clientSideWindowChrome->property("contentTop").toReal(), 0.0);

    windowCommands.windowChromeController().setState(false, false, false);
    QVERIFY(!openingState->property("visible").toBool());
    QVERIFY(!errorState->property("visible").toBool());
    QVERIFY(!seekSlider->property("visible").toBool());
    supportController.requestHdrLab();
    QTRY_VERIFY(!updateButton->isVisible());
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
    QTRY_VERIFY(!updateButton->isVisible());
    QTRY_VERIFY(!updateError->isVisible());
    QTRY_VERIFY(openingState->property("visible").toBool());
    QVERIFY(!videoViewport.visible());
    QVERIFY(QMetaObject::invokeMethod(cancelOpenButton, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.cancelCount(), 1);
    QCOMPARE(mediaSession.state(), ShellTestMediaSession::State::Empty);

    // Audio output can make the session ready before the first video frame.
    // Keep the playback chrome and presentation viewport active so that frame
    // selection can make progress instead of deadlocking behind hasFrame.
    mediaSession.setState(ShellTestMediaSession::State::Ready, false);
    QTRY_VERIFY(!updateButton->isVisible());
    QVERIFY(QMetaObject::invokeMethod(playerPage, "revealControls", Qt::DirectConnection));
    QTRY_VERIFY(videoViewport.visible());
    QTRY_VERIFY(!idleMoreButton->property("visible").toBool());
    QTRY_VERIFY(playbackActivityState->property("visible").toBool());
    QCOMPARE(playbackActivityIndicator->property("width").toInt(), 44);
    QCOMPARE(playbackActivityIndicator->property("height").toInt(), 44);
    QVERIFY(playbackActivityIndicator->property("running").toBool());
    QCOMPARE(playbackActivityLabel->property("text").toString(), QStringLiteral("Preparing video…"));
    QCOMPARE(QQmlProperty(playbackActivityLabel, QStringLiteral("font.pixelSize")).read().toInt(), 15);
    QVERIFY(playbackActivityState->property("color").value<QColor>().alphaF() > 0.5f);
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
    // Qt can deliver hover updates without pointer movement. CSD must not
    // interpret those updates as activity and continually restart the timer.
    for (int mode = 0; mode < 3; ++mode) {
        windowCommands.windowChromeController().setState(true, mode == 1, mode == 2);
        QPointF pointerPosition(300, 200);
        ulong timestamp = 0;
        auto const deliverPointer = [&] {
            QMouseEvent event(QEvent::MouseMove, pointerPosition, pointerPosition, Qt::NoButton, Qt::NoButton,
                              Qt::NoModifier);
            event.setTimestamp(++timestamp);
            QCoreApplication::sendEvent(&quickWindow, &event);
        };
        QTimer stationaryPointer;
        connect(&stationaryPointer, &QTimer::timeout, &quickWindow, deliverPointer);
        stationaryPointer.start(16);
        QVERIFY(QMetaObject::invokeMethod(playerPage, "revealControls", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(transportIsland->opacity() < 0.05, 4000);
        QTRY_VERIFY(windowCommands.cursorHidden());
        pointerPosition.rx() += 20;
        deliverPointer();
        QTRY_VERIFY(transportIsland->opacity() > 0.9);
        QTRY_VERIFY(!windowCommands.cursorHidden());
    }
    windowCommands.windowChromeController().setState(false, false, false);
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
    QTRY_VERIFY(!playbackActivityState->property("visible").toBool());
    mediaSession.setState(ShellTestMediaSession::State::Error);
    QTRY_VERIFY(!updateButton->isVisible());
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
    QTRY_VERIFY(playbackActivityState->property("visible").toBool());
    QVERIFY(playbackActivityIndicator->property("visible").toBool());
    QVERIFY(playbackActivityIndicator->property("running").toBool());
    QCOMPARE(playbackActivityIndicator->property("width").toInt(), 44);
    QCOMPARE(QQmlProperty(playbackActivityLabel, QStringLiteral("font.pixelSize")).read().toInt(), 15);
    QVERIFY(playbackActivityLabel->property("text").toString().contains(QStringLiteral("1:00")));
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seekCountAfterSlider + 1);
    QCOMPARE(mediaSession.lastSeekMilliseconds(), 60'000);
    QVERIFY(!playerPage->property("relativeSeekPending").toBool());

    // A native seek or Stop supersedes a pending UI preview.
    mediaSession.setTimeline(30'000, 65'000, true);
    emit windowCommands.relativeSeekRequested(10'000);
    QVERIFY(playerPage->property("relativeSeekPending").toBool());
    emit mediaSession.positionJumped(15'000);
    QVERIFY(!playerPage->property("relativeSeekPending").toBool());
    int const seeksBeforeNativeStop = mediaSession.seekCount();
    emit windowCommands.relativeSeekRequested(10'000);
    mediaSession.stop();
    QVERIFY(!playerPage->property("relativeSeekPending").toBool());
    QVERIFY(QMetaObject::invokeMethod(playerPage, "dispatchPendingRelativeSeek", Qt::DirectConnection));
    QCOMPARE(mediaSession.seekCount(), seeksBeforeNativeStop);
    mediaSession.play();

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

    // A slow seek clears decoder-backed video and rebuilds audio output, but
    // keeps the playback controls usable. New relative commands still form a
    // single latest-wins replacement request.
    mediaSession.setState(ShellTestMediaSession::State::Opening, false);
    mediaSession.setHasAudioOutput(false);
    mediaSession.setTimeline(42'000, 65'000, true, true);
    QTRY_VERIFY(playbackActivityState->property("visible").toBool());
    QVERIFY(!openingState->property("visible").toBool());
    QVERIFY(seekSlider->property("enabled").toBool());
    QVERIFY(seekBackwardButton->property("enabled").toBool());
    QVERIFY(seekForwardButton->property("enabled").toBool());
    QVERIFY(playPauseButton->property("enabled").toBool());
    QVERIFY(muteButton->property("visible").toBool());
    QVERIFY(volumeSlider->property("visible").toBool());
    QVERIFY(videoViewport.visible());
    QCOMPARE(playbackActivityIndicator->property("width").toInt(), 44);
    QCOMPARE(playbackActivityIndicator->property("height").toInt(), 44);
    QVERIFY(playbackActivityIndicator->property("running").toBool());

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
    mediaSession.setHasAudioOutput(true);
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

    supportController.requestHdrLab();
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

    auto* const playerBack = rootItem->findChild<QQuickItem*>(QStringLiteral("playerBackButton"));
    auto* const openJellyfin = rootItem->findChild<QQuickItem*>(QStringLiteral("openJellyfinButton"));
    auto* const browserBack = rootItem->findChild<QQuickItem*>(QStringLiteral("jellyfinBackButton"));
    auto* const detailsItem = qobject_cast<QQuickItem*>(playbackDetailsPanel);
    QVERIFY(playerBack);
    QVERIFY(openJellyfin);
    QVERIFY(browserBack);
    QVERIFY(detailsItem);
    mediaSession.pause();
    playerPage->setProperty("showPlaybackDetails", true);
    for (auto const [csd, fullscreen] : {std::pair{true, false}, std::pair{true, true}, std::pair{false, false}}) {
        windowCommands.windowChromeController().setState(csd, fullscreen, false);
        qreal const inset = csd && !fullscreen ? clientSideTitleBar->height() : 0;
        QTRY_COMPARE(playerBack->mapToScene(QPointF()).y(), inset + 24);
        QTRY_COMPARE(detailsItem->mapToScene(QPointF()).y(), inset + 20);
        QTRY_VERIFY(detailsItem->mapToScene({0, detailsItem->height()}).y() <= rootItem->height() - 20);
        QTRY_COMPARE(videoViewport.rect().y(), 0.0);
        QTRY_COMPARE(videoViewport.rect().height(), rootItem->height());
    }

    // Jellyfin playback returns to the retained browser; the browser's Back
    // returns to the Open video screen. Local playback returns there directly.
    mediaSession.setState(ShellTestMediaSession::State::Empty);
    QTRY_VERIFY(openJellyfin->isVisible());
    QVERIFY(!playerBack->isVisible());
    QVERIFY(QMetaObject::invokeMethod(openJellyfin, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 2);
    windowCommands.windowChromeController().setState(true, false, false);
    QTRY_VERIFY(browserBack->mapToScene(QPointF()).y() >= clientSideTitleBar->height() + 24);
    emit windowCommands.mediaOpenRequested();
    mediaSession.setState(ShellTestMediaSession::State::Ready, true);
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(playerBack, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.state(), ShellTestMediaSession::State::Empty);
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(browserBack, "clicked", Qt::DirectConnection));
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 0);
    QTRY_VERIFY(openMediaButton->property("visible").toBool());
    QTest::mouseClick(&quickWindow, Qt::ForwardButton, Qt::NoModifier, QPoint(500, 300));
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 2);
    QTest::mouseClick(&quickWindow, Qt::BackButton, Qt::NoModifier, QPoint(500, 300));
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 0);
    emit windowCommands.navigationForwardRequested();
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 2);
    emit windowCommands.navigationBackRequested();
    QTRY_COMPARE(rootItem->property("currentPage").toInt(), 0);
    emit windowCommands.mediaOpenRequested();
    mediaSession.setState(ShellTestMediaSession::State::Ready, true);
    QVERIFY(QMetaObject::invokeMethod(playerBack, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.state(), ShellTestMediaSession::State::Empty);
    QCOMPARE(rootItem->property("currentPage").toInt(), 0);

    // A direct launch can establish playback before QML exists. With no open
    // event/history, Back must still return to Open video.
    mediaSession.setState(ShellTestMediaSession::State::Ready, true);
    std::unique_ptr<QObject> directLaunch(component.createWithInitialProperties(initialProperties));
    QVERIFY2(directLaunch, qPrintable(componentErrorText(component)));
    auto* const directBack = directLaunch->findChild<QObject*>(QStringLiteral("playerBackButton"));
    QVERIFY(directBack);
    QVERIFY(QMetaObject::invokeMethod(directBack, "clicked", Qt::DirectConnection));
    QCOMPARE(mediaSession.state(), ShellTestMediaSession::State::Empty);
    QCOMPARE(directLaunch->property("currentPage").toInt(), 0);
    directLaunch.reset();

    // Populate the real browser through a controlled HTTP edge, including its
    // authenticated image provider. Capture the actual shell under both styles.
    HttpEdge galleryServer;
    galleryServer.galleryFixtures = true;
    engine.addImageProvider(QStringLiteral("jellyfin"), new JellyfinImages(jellyfin.client()));
    QSignalSpy qmlWarnings(&engine, &QQmlEngine::warnings);
    rootItem->setProperty("currentPage", 2);
    windowCommands.windowChromeController().setState(true, false, false);
    jellyfin.login(galleryServer.base().toString(), QStringLiteral("user"), QStringLiteral("password"), false);
    QTRY_VERIFY(jellyfin.authenticated() && !jellyfin.busy());
    auto imagesReady = [&] {
        int images = 0;
        bool ready = true;
        auto visit = [&](auto&& self, QQuickItem* parent) -> void {
            for (auto* item : parent->childItems()) {
                if (item->property("source").toUrl().scheme() == QStringLiteral("image")) {
                    ++images;
                    ready &= item->property("status").toInt() == 1;
                }
                self(self, item);
            }
        };
        visit(visit, rootItem);
        return images > 0 && ready;
    };
    QDir().mkpath(QStringLiteral(SUNPLAYER_UI_SNAPSHOT_DIR));
    auto snapshot = [&](QString const& name) {
        QImage const image = quickWindow.grabWindow();
        return !image.isNull() &&
               image.save(QStringLiteral(SUNPLAYER_UI_SNAPSHOT_DIR "/%1-%2.png").arg(name, QQuickStyle::name()));
    };
    auto* tabs = rootItem->findChild<QQuickItem*>(QStringLiteral("jellyfinTabs"));
    auto* title = rootItem->findChild<QQuickItem*>(QStringLiteral("jellyfinTitle"));
    auto* browserBackground = rootItem->findChild<QQuickItem*>(QStringLiteral("jellyfinBackground"));
    QVERIFY(tabs && title && browserBackground);
    QCOMPARE(browserBackground->property("color").value<QColor>(), QColor(QStringLiteral("#181818")));
    for (int width : {1280, 800, 760}) {
        rootItem->setSize({qreal(width), 900});
        quickWindow.resize(width, 900);
        QTRY_VERIFY(imagesReady());
        if (width == 1280) {
            auto findCard = [&](auto&& self, QQuickItem* parent) -> QQuickItem* {
                if (parent->isVisible() && parent->property("item").isValid() &&
                    parent->property("landscape").isValid()) {
                    return parent;
                }
                for (auto* child : parent->childItems()) {
                    if (auto* card = self(self, child)) {
                        return card;
                    }
                }
                return nullptr;
            };
            auto* card = findCard(findCard, rootItem);
            QVERIFY(card);
            QPoint const cardPoint = card->mapToScene(QPointF(card->width() / 2, 30)).toPoint();
            QTest::mouseMove(&quickWindow, cardPoint);
            QTRY_VERIFY(card->property("hovered").toBool());
            QTest::mousePress(&quickWindow, Qt::LeftButton, Qt::NoModifier, cardPoint);
            QTest::mouseMove(&quickWindow, QPoint(10, 10));
            QTest::mouseRelease(&quickWindow, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
            QTRY_VERIFY(!card->property("hovered").toBool());
            QVERIFY(!card->property("pressed").toBool());
            QTest::mouseMove(&quickWindow, cardPoint);
            QTRY_VERIFY(card->property("hovered").toBool());
            QTest::mouseMove(&quickWindow, QPoint(10, 10));
            QTRY_VERIFY(!card->property("hovered").toBool());
        }
        QTRY_VERIFY(tabs->width() > 0);
        QTRY_VERIFY(std::abs(tabs->mapToScene(QPointF(0, tabs->height() / 2)).y() -
                             title->mapToScene(QPointF(0, title->height() / 2)).y()) < 1);
        QVERIFY(snapshot(QStringLiteral("jellyfin-home-%1").arg(width)));
    }
    rootItem->setSize({800, 900});
    quickWindow.resize(800, 900);
    jellyfin.selectLibrary(0);
    QTRY_VERIFY(!jellyfin.busy());
    QTRY_VERIFY(rootItem->findChild<QQuickItem*>(QStringLiteral("jellyfinItems")));
    QTRY_VERIFY(imagesReady());
    QVERIFY(snapshot(QStringLiteral("jellyfin-movies-800")));
    jellyfin.home();
    QTRY_VERIFY(!jellyfin.busy());
    jellyfin.selectLibrary(1);
    QTRY_VERIFY(!jellyfin.busy());
    jellyfin.enter(0);
    QTRY_VERIFY(!jellyfin.busy());
    QTRY_VERIFY(imagesReady());
    QTest::mouseClick(&quickWindow, Qt::BackButton, Qt::NoModifier, QPoint(700, 180));
    QTRY_VERIFY(!jellyfin.busy());
    QCOMPARE(jellyfin.currentItem().value(QStringLiteral("kind")).toString(), QStringLiteral("CollectionFolder"));
    QTest::mouseClick(&quickWindow, Qt::ForwardButton, Qt::NoModifier, QPoint(700, 180));
    QTRY_VERIFY(!jellyfin.busy());
    QCOMPARE(jellyfin.currentItem().value(QStringLiteral("kind")).toString(), QStringLiteral("Series"));
    QTRY_VERIFY(imagesReady());
    auto* gallery = rootItem->findChild<QQuickItem*>(QStringLiteral("jellyfinItems"));
    QVERIFY(gallery);
    auto* header = gallery->property("headerItem").value<QQuickItem*>();
    QVERIFY(header);
    auto* detail = header->findChild<QQuickItem*>(QStringLiteral("jellyfinDetailHeader"));
    QVERIFY(detail);
    QTRY_COMPARE(detail->mapToScene(QPointF()).x(), 0.0);
    QTRY_COMPARE(detail->width(), rootItem->width());
    QTRY_VERIFY(header->height() - detail->height() >= 20);
    QVERIFY(snapshot(QStringLiteral("jellyfin-show-800")));
    jellyfin.enter(0);
    QTRY_VERIFY(!jellyfin.busy());
    QTRY_VERIFY(rootItem->findChild<QQuickItem*>(QStringLiteral("jellyfinEpisodes")));
    QTRY_VERIFY(imagesReady());
    QVERIFY(snapshot(QStringLiteral("jellyfin-season-800")));
    rootItem->setSize({640, 900});
    quickWindow.resize(640, 900);
    QTRY_VERIFY(imagesReady());
    QVERIFY(snapshot(QStringLiteral("jellyfin-season-640")));
    QCOMPARE(qmlWarnings.size(), 0);
}

void AppShellTest::transportButtonShowsKeyboardFocus() {
    ShellTestPresentationOutputState outputState(nullptr);
    ShellTestWindowCommands windowCommands(nullptr);
    ShellTestPresentationSettings presentationSettings(nullptr);
    ShellTestDiagnosticVideoSource videoSource(nullptr);
    ShellTestMediaSession mediaSession(nullptr);
    ShellTestActiveVideoSource activeVideoSource(nullptr);
    ShellTestSupportController supportController(nullptr);
    VideoViewportState videoViewport(nullptr);
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SUNPLAYER_QT_QML_IMPORT_PATH));
    QQmlComponent component(&engine);
    component.loadFromModule(QStringLiteral("SunPlayerShellTest"), QStringLiteral("Main"));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {QStringLiteral("renderDevicePixelRatio"), 1.0},
        {QStringLiteral("applicationDisplayName"), QStringLiteral("SunPlayer (Test)")},
        {QStringLiteral("windowCommands"), QVariant::fromValue(&windowCommands)},
        {QStringLiteral("presentationOutput"), QVariant::fromValue(&outputState)},
        {QStringLiteral("presentationPolicy"), QVariant::fromValue(&presentationSettings)},
        {QStringLiteral("diagnosticSource"), QVariant::fromValue(&videoSource)},
        {QStringLiteral("mediaSession"), QVariant::fromValue(&mediaSession)},
        {QStringLiteral("activeVideoSource"), QVariant::fromValue(&activeVideoSource)},
        {QStringLiteral("viewportState"), QVariant::fromValue(&videoViewport)},
        {QStringLiteral("supportController"), QVariant::fromValue(&supportController)},
    }));
    QVERIFY2(object, qPrintable(componentErrorText(component)));
    auto* const root = qobject_cast<QQuickItem*>(object.get());
    QVERIFY(root);
    auto* const button = root->findChild<QQuickItem*>(QStringLiteral("moreButton"));
    QVERIFY(button);
    auto* const background = qvariant_cast<QObject*>(button->property("background"));
    QVERIFY(background);
    QQuickWindow window;
    window.resize(1100, 760);
    root->setParentItem(window.contentItem());
    root->setSize(window.size());
    window.show();
    QTRY_VERIFY(window.isExposed());
    mediaSession.setState(ShellTestMediaSession::State::Ready);
    QTRY_VERIFY(button->isVisible() && button->isEnabled());
    QCOMPARE(background->property("color").value<QColor>().alpha(), 0);
    button->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(button->property("visualFocus").toBool());
    QVERIFY(background->property("color").value<QColor>().alpha() > 0);
    window.contentItem()->forceActiveFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(!button->property("visualFocus").toBool());
    QCOMPARE(background->property("color").value<QColor>().alpha(), 0);
}

void AppShellTest::appScrollbarPreservesPlatformPalette() {
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SUNPLAYER_QT_QML_IMPORT_PATH));
    QQmlComponent component(&engine);
    component.loadFromModule(QStringLiteral("SunPlayerShellTest"), QStringLiteral("AppScrollBar"));
    std::unique_ptr<QObject> scrollbar(component.create());
    QVERIFY2(scrollbar, qPrintable(componentErrorText(component)));
    auto color = [](QObject* control, char const* role) {
        return QQmlProperty(control, QString::fromLatin1(role)).read().value<QColor>();
    };
#ifdef Q_OS_WIN
    QCOMPARE(color(scrollbar.get(), "palette.mid"), QColor(QStringLiteral("#888888")));
    QCOMPARE(color(scrollbar.get(), "palette.dark"), QColor(QStringLiteral("#aaaaaa")));
#else
    QQmlComponent stockComponent(&engine);
    stockComponent.setData("import QtQuick.Controls; ScrollBar {}", QUrl());
    std::unique_ptr<QObject> stock(stockComponent.create());
    QVERIFY2(stock, qPrintable(componentErrorText(stockComponent)));
    QCOMPARE(color(scrollbar.get(), "palette.mid"), color(stock.get(), "palette.mid"));
    QCOMPARE(color(scrollbar.get(), "palette.dark"), color(stock.get(), "palette.dark"));
#endif
}

QTEST_MAIN(AppShellTest)

#include "tst_AppShell.moc"
