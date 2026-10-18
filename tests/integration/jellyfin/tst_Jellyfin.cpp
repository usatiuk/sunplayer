#include <QScopeGuard>
#include <future>
#include <mutex>
extern "C" {
#include <libavutil/log.h>
}
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include "jellyfin/JellyfinBrowser.h"
#include "jellyfin/JellyfinClient.h"
#include "jellyfin/JellyfinPlaybackAdapter.h"
#include "media/FfmpegMediaDecoder.h"

#include "HttpEdge.h"

class UntrustedTlsEdge final : public QTcpServer {
  public:
    int accepted = 0;

  protected:
    void incomingConnection(qintptr descriptor) override {
        ++accepted;
        auto* socket = new QSslSocket(this);
        QFile certificate(QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/network/untrusted-server.pem"));
        QFile key(QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/network/untrusted-server-key.pem"));
        if (!certificate.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly)) {
            qFatal("Missing TLS test fixtures");
        }
        socket->setLocalCertificate(QSslCertificate(certificate.readAll()));
        socket->setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        socket->startServerEncryption();
    }
};

class JellyfinTest final : public QObject {
    Q_OBJECT
  private slots:
    void authenticatedApiAndDirectPlay();
    void homeShelvesAndDetails();
    void serverAddressDefaultsToHttps();
    void httpDecodeAndInitialResume();
    void stalledHttpCancels();
    void browserAndFinalReportOrdering();
    void authenticationLossClosesOldPlayback();
    void videoOnlyStallFreezesAndRecovers();
    void redirectDoesNotForwardCredentials();
    void untrustedTlsIsRejected();
    void failedOpenRedactsCredentials();
};

void JellyfinTest::authenticatedApiAndDirectPlay() {
    HttpEdge edge;
    JellyfinClient client;
    bool done = false;
    QString failure;
    client.login(edge.base(), QStringLiteral("user"), QStringLiteral("password"), QStringLiteral("device123"),
                 [&](QString error) {
                     failure = error;
                     done = true;
                 });
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(client.authenticated());
    QCOMPARE(edge.requests.front().body.value(QStringLiteral("Pw")).toString(), QStringLiteral("password"));
    JellyfinItem item{.id = QStringLiteral("item123"), .name = QStringLiteral("Example"), .positionMilliseconds = 2500};
    JellyfinPlayback playback;
    done = false;
    client.resolve(
        item, false,
        [&](JellyfinPlayback value, QString error) {
            playback = value;
            failure = error;
            done = true;
        },
        this);
    QTRY_VERIFY(done);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(playback.input.isValid());
    QCOMPARE(playback.startingPositionMilliseconds, 2500);
    QVERIFY(!playback.input.publicUrl().toString().contains(QStringLiteral("test-token")));
    QVERIFY(edge.requests.back().headers.contains("Token=\"test-token\""));
    QCOMPARE(edge.requests.back().method, QByteArray("GET"));
    QCOMPARE(edge.requests.back().contentLength, 0);
    QCOMPARE(QUrlQuery(edge.requests.back().url).queryItemValue(QStringLiteral("UserId")), QStringLiteral("abc123"));
    QCOMPARE(QUrlQuery(playback.input.locator).queryItemValue(QStringLiteral("Static")), QStringLiteral("true"));
    done = false;
    client.report(
        playback, true, 2750, true,
        [&](QString error) {
            failure = error;
            done = true;
        },
        this);
    QTRY_VERIFY(done);
    QVERIFY(failure.isEmpty());
    QCOMPARE(edge.requests.back().body.value(QStringLiteral("PositionTicks")).toDouble(), 27500000.0);
}

void JellyfinTest::homeShelvesAndDetails() {
    HttpEdge edge;
    edge.galleryFixtures = true;
    QTemporaryDir directory;
    JellyfinBrowser browser(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat, false);
    browser.login(edge.base().toString(), QStringLiteral("user"), QStringLiteral("password"), false);
    QTRY_VERIFY(browser.authenticated() && !browser.busy());
    QCOMPARE(browser.libraries().size(), 2);
    QCOMPARE(browser.continueWatching().front().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("Episode"));
    QCOMPARE(browser.recentMovies().front().toMap().value(QStringLiteral("year")).toInt(), 2024);
    QCOMPARE(browser.recentShows().front().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Beyond the Ordinary"));
    for (auto const& request : edge.requests) {
        if (request.url.path().endsWith(QStringLiteral("/Resume")) ||
            request.url.path().endsWith(QStringLiteral("/Items"))) {
            QUrlQuery query(request.url);
            QCOMPARE(query.queryItemValue(QStringLiteral("Limit")), QStringLiteral("12"));
            if (request.url.path().endsWith(QStringLiteral("/Resume"))) {
                QCOMPARE(query.queryItemValue(QStringLiteral("IncludeItemTypes")), QStringLiteral("Movie,Episode"));
            } else {
                QCOMPARE(query.queryItemValue(QStringLiteral("SortBy")), QStringLiteral("DateCreated"));
                QCOMPARE(query.queryItemValue(QStringLiteral("SortOrder")), QStringLiteral("Descending"));
                QCOMPARE(query.queryItemValue(QStringLiteral("Recursive")), QStringLiteral("true"));
                QVERIFY((QStringList{QStringLiteral("Movie"), QStringLiteral("Series")}.contains(
                    query.queryItemValue(QStringLiteral("IncludeItemTypes")))));
            }
        }
    }
    QSignalSpy play(&browser, &JellyfinBrowser::playRequested);
    browser.openHomeItem(JellyfinBrowser::ContinueWatching, 0);
    QCOMPARE(play.size(), 1);
    QCOMPARE(qvariant_cast<JellyfinItem>(play.back()[0]).id, QStringLiteral("episodeHome"));
    browser.openHomeItem(JellyfinBrowser::RecentMovies, 0);
    QCOMPARE(browser.currentItem().value(QStringLiteral("itemId")).toString(), QStringLiteral("movieHome"));
    QVERIFY(!browser.hasMore());
    QTRY_VERIFY(!browser.busy());
    QVERIFY(edge.requests.back().url.path().endsWith(QStringLiteral("/Items/movieHome")));
    browser.playCurrent();
    QCOMPARE(qvariant_cast<JellyfinItem>(play.back()[0]).id, QStringLiteral("movieHome"));
    browser.refresh();
    QTRY_VERIFY(!browser.busy());
    QCOMPARE(browser.currentItem().value(QStringLiteral("name")).toString(), QStringLiteral("The Last Horizon"));
    browser.home();
    QTRY_VERIFY(!browser.busy());
    edge.failResume = true;
    browser.retry();
    QTRY_VERIFY(!browser.busy());
    QVERIFY(browser.message().contains(QStringLiteral("Home shelf")));
    QCOMPARE(browser.libraries().size(), 2);
    browser.selectLibrary(1);
    QTRY_VERIFY(!browser.busy());
    browser.enter(0);
    QTRY_VERIFY(!browser.busy());
    QCOMPARE(browser.currentItem().value(QStringLiteral("kind")).toString(), QStringLiteral("Series"));
    browser.enter(0);
    QTRY_VERIFY(!browser.busy());
    QCOMPARE(browser.currentItem().value(QStringLiteral("kind")).toString(), QStringLiteral("Season"));
    QCOMPARE(browser.data(browser.index(0), JellyfinBrowser::Subtitle).toString(),
             QStringLiteral("Beyond the Ordinary · S1 · E1"));
    edge.failResume = false;
    edge.delayFeeds = true;
    browser.home();
    QTRY_COMPARE(edge.delayedFeeds.size(), 3);
    bool lost = false;
    browser.client().request("GET", QStringLiteral("force401"), {}, {}, [&](QJsonObject, QString) { lost = true; });
    QTRY_VERIFY(lost && !browser.authenticated());
    edge.releaseFeeds();
    bool barrier = false;
    browser.client().request("GET", QStringLiteral("settled"), {}, {}, [&](QJsonObject, QString) { barrier = true; });
    QTRY_VERIFY(barrier);
    QVERIFY(browser.libraries().isEmpty());
    QVERIFY(browser.continueWatching().isEmpty());
    QVERIFY(browser.recentMovies().isEmpty());
    QCOMPARE(browser.rowCount(), 0);
}

void JellyfinTest::serverAddressDefaultsToHttps() {
    HttpEdge edge;
    auto const bare = edge.base().toString().mid(QStringLiteral("http://").size());
    for (auto const& address : {bare, edge.base().toString(), QStringLiteral("https://") + bare}) {
        QTemporaryDir directory;
        JellyfinBrowser browser(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat, false);
        browser.login(QStringLiteral("  ") + address + QStringLiteral("  "), QStringLiteral("user"),
                      QStringLiteral("password"), false);
        auto const expectedScheme =
            address.startsWith(QStringLiteral("http://")) ? QStringLiteral("http") : QStringLiteral("https");
        QCOMPARE(browser.client().server().scheme(), expectedScheme);
        QCOMPARE(browser.client().server().host(), edge.base().host());
        QCOMPARE(browser.client().server().port(), edge.base().port());
        QCOMPARE(browser.client().server().path(), QStringLiteral("/jellyfin"));
        browser.client().abortRequests();
    }
}

void JellyfinTest::httpDecodeAndInitialResume() {
    HttpEdge edge;
    auto decode = [&](bool resume) {
        FfmpegMediaDecodeRequest request;
        request.video.input = MediaInput(edge.stream(), QStringLiteral("Example"));
        request.video.firstFrameIdentity = {1, 1, 1};
        request.decodeSelectedAudio = false;
        if (resume) {
            request.video.start.targetPositionMicroseconds = 2500000;
            request.video.start.performDemuxSeek = true;
        }
        return std::async(std::launch::async, [request] {
            qlonglong first = -1;
            auto result = decodeMediaFrames(
                request,
                [&](auto frame, auto const&) {
                    first = frame->timing().ptsMicroseconds().value_or(-1);
                    return false;
                },
                FfmpegPcmAudioSink{});
            return std::pair{result, first};
        });
    };
    auto initial = decode(false);
    QTRY_VERIFY_WITH_TIMEOUT(initial.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
    auto first = initial.get();
    QVERIFY2(first.first.isSuccess() || first.first.isStopped(), qPrintable(first.first.error));
    auto resumed = decode(true);
    QTRY_VERIFY_WITH_TIMEOUT(resumed.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
    auto sought = resumed.get();
    QVERIFY2(sought.first.isSuccess() || sought.first.isStopped(), qPrintable(sought.first.error));
    QCOMPARE(first.second, 0);
    QCOMPARE(sought.second, 2000000);
    bool ranged = false;
    for (auto const& request : edge.requests) {
        ranged |= request.headers.contains("Range: bytes=");
    }
    QVERIFY(ranged);
}

void JellyfinTest::stalledHttpCancels() {
    HttpEdge edge;
    edge.stall = true;
    std::stop_source stop;
    auto future = std::async(std::launch::async, [&] {
        FfmpegMediaDecodeRequest request;
        request.video.input = MediaInput(edge.stream(), QStringLiteral("Stalled"));
        request.video.firstFrameIdentity = {1, 1, 1};
        request.decodeSelectedAudio = false;
        return decodeMediaFrames(
            request, [](auto, auto const&) { return true; }, FfmpegPcmAudioSink{}, stop.get_token());
    });
    QTRY_VERIFY(!edge.requests.isEmpty());
    stop.request_stop();
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 3000);
    QVERIFY(future.get().isCancelled());
}

void JellyfinTest::browserAndFinalReportOrdering() {
    HttpEdge edge;
    QTemporaryDir directory;
    JellyfinBrowser browser(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat, false);
    // Session-only login never writes native credentials.
    browser.login(edge.base().toString(), QStringLiteral("user"), QStringLiteral("password"), false);
    QTRY_VERIFY(browser.authenticated() && !browser.busy() && browser.libraries().size() == 1);
    browser.selectLibrary(0);
    QTRY_VERIFY(!browser.busy() && browser.rowCount() == 2);
    QCOMPARE(browser.data(browser.index(0), JellyfinBrowser::Overview).toString(),
             QStringLiteral("A show description."));
    QCOMPARE(browser.data(browser.index(0), JellyfinBrowser::Progress).toDouble(), 0.25);
    QVERIFY(browser.canGoBack());

    MediaSession player(VideoTargetReadback::Disabled,
                        [](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink,
                           std::stop_token token) { return decodeVideoFrames(request, sink, token); });
    JellyfinPlaybackAdapter adapter(browser, player);
    edge.delayStop = true;
    browser.play(0, false);
    QTRY_VERIFY_WITH_TIMEOUT(player.state() == MediaSession::State::Ready && player.hasFrame(), 10000);
    player.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
    QTRY_VERIFY(std::any_of(edge.requests.cbegin(), edge.requests.cend(),
                            [](auto const& r) { return r.url.path().endsWith(QStringLiteral("/Playing")); }));
    QVERIFY(player.positionMilliseconds() >= 2500);
    QTRY_VERIFY(!adapter.pending());
    player.pause();
    auto const finalPosition = player.positionMilliseconds();
    auto const firstId = player.mediaItemId();
    browser.play(1, true);
    QTRY_VERIFY(!edge.delayedStops.isEmpty());
    auto const stoppedRequest = std::find_if(edge.requests.crbegin(), edge.requests.crend(), [](auto const& request) {
        return request.url.path().endsWith(QStringLiteral("/Stopped"));
    });
    QVERIFY(stoppedRequest != edge.requests.crend());
    auto const stop = stoppedRequest->body;
    QCOMPARE(stop.value(QStringLiteral("ItemId")).toString(), QStringLiteral("item123"));
    QCOMPARE(stop.value(QStringLiteral("PlaySessionId")).toString(), QStringLiteral("item123-session"));
    QCOMPARE(stop.value(QStringLiteral("PositionTicks")).toDouble(), double(finalPosition) * 10000);
    QTRY_VERIFY(player.mediaItemId() != firstId && player.state() == MediaSession::State::Ready);
    auto starts = [&] {
        return std::count_if(edge.requests.cbegin(), edge.requests.cend(),
                             [](auto const& r) { return r.url.path().endsWith(QStringLiteral("/Playing")); });
    };
    QCOMPARE(starts(), 1);
    edge.releaseStops();
    QTRY_COMPARE(starts(), 2);
    auto secondStart = std::find_if(edge.requests.crbegin(), edge.requests.crend(),
                                    [](auto const& r) { return r.url.path().endsWith(QStringLiteral("/Playing")); });
    QCOMPARE(secondStart->body.value(QStringLiteral("ItemId")).toString(), QStringLiteral("item456"));
    QCOMPARE(secondStart->body.value(QStringLiteral("PlaySessionId")).toString(), QStringLiteral("item456-session"));
    QTRY_VERIFY(!browser.busy() && !adapter.pending());
    edge.delayWatched = true;
    browser.setWatched(1, true);
    QTRY_VERIFY(!edge.delayedStops.isEmpty());
    QVERIFY(browser.busy());
    QVERIFY(std::none_of(edge.requests.cbegin(), edge.requests.cend(),
                         [](auto const& r) { return r.url.path().contains(QStringLiteral("PlayedItems")); }));
    edge.releaseStops();
    QTRY_VERIFY(std::any_of(edge.requests.cbegin(), edge.requests.cend(),
                            [](auto const& r) { return r.url.path().contains(QStringLiteral("PlayedItems")); }));
    QTRY_COMPARE(edge.delayedWatched.size(), 1);
    bool refreshSettled = false;
    browser.client().request("GET", QStringLiteral("settled"), {}, {},
                             [&](QJsonObject, QString) { refreshSettled = true; });
    QTRY_VERIFY(refreshSettled);
    QVERIFY(browser.busy());
    HttpEdge::respondJson(edge.delayedWatched.front(), {});
    QTRY_VERIFY(!browser.busy());
    QVERIFY(player.state() == MediaSession::State::Empty);
}

void JellyfinTest::authenticationLossClosesOldPlayback() {
    HttpEdge oldServer;
    HttpEdge newServer;
    QTemporaryDir directory;
    JellyfinBrowser browser(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat, false);
    MediaSession player(VideoTargetReadback::Disabled,
                        [](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink,
                           std::stop_token token) { return decodeVideoFrames(request, sink, token); });
    JellyfinPlaybackAdapter adapter(browser, player);
    browser.login(oldServer.base().toString(), QStringLiteral("user"), QStringLiteral("password"), false);
    QTRY_VERIFY(browser.authenticated() && !browser.busy());
    browser.selectLibrary(0);
    QTRY_VERIFY(!browser.busy() && browser.rowCount() == 2);
    browser.play(0, false);
    QTRY_VERIFY(player.state() == MediaSession::State::Ready && !adapter.pending());
    oldServer.delayStop = true;
    browser.back();
    QTRY_VERIFY(!browser.busy());
    oldServer.rejectLibraries = true;
    browser.refresh();
    QTRY_VERIFY(!browser.authenticated() && !oldServer.delayedStops.isEmpty());
    QCOMPARE(player.state(), MediaSession::State::Empty);
    QVERIFY(browser.busy());
    browser.login(newServer.base().toString(), QStringLiteral("user"), QStringLiteral("password"), false);
    QVERIFY(newServer.requests.isEmpty());
    oldServer.releaseStops();
    QTRY_VERIFY(!browser.busy());
    browser.login(newServer.base().toString(), QStringLiteral("user"), QStringLiteral("password"), false);
    QTRY_VERIFY(browser.authenticated() && !browser.busy());
    QVERIFY(std::none_of(newServer.requests.cbegin(), newServer.requests.cend(),
                         [](auto const& r) { return r.url.path().contains(QStringLiteral("Sessions/Playing")); }));
}

void JellyfinTest::videoOnlyStallFreezesAndRecovers() {
    HttpEdge edge;
    edge.pauseMedia = true;
    MediaSession player(VideoTargetReadback::Disabled,
                        [](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink,
                           std::stop_token token) { return decodeVideoFrames(request, sink, token); });
    player.openMedia(MediaInput(edge.stream(), QStringLiteral("Video-only stall")));
    QTRY_VERIFY_WITH_TIMEOUT(player.state() == MediaSession::State::Ready, 5000);
    // Let the production monitor exhaust the received data naturally. Mutating
    // presentation time inside a QTRY predicate can alternate stall/recovery
    // while buffered decoder work is still arriving.
    QTRY_VERIFY_WITH_TIMEOUT(player.playbackInterruption() == MediaSession::PlaybackInterruption::Buffering, 15000);
    auto const frozen = player.positionMilliseconds();
    player.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
    QCOMPARE(player.positionMilliseconds(), frozen);
    QVERIFY(!edge.pausedMedia.isEmpty());
    edge.releaseMedia();
    QTRY_VERIFY_WITH_TIMEOUT(player.playbackInterruption() == MediaSession::PlaybackInterruption::None, 5000);
    player.cancel();
}

void JellyfinTest::redirectDoesNotForwardCredentials() {
    HttpEdge edge;
    HttpEdge redirected;
    edge.redirect = QUrl(redirected.base().toString() + QStringLiteral("/Videos/item123/stream"));
    auto future = std::async(std::launch::async, [&] {
        FfmpegMediaDecodeRequest request;
        request.video.input = MediaInput(edge.stream(), QStringLiteral("Redirect"));
        request.video.firstFrameIdentity = {1, 1, 1};
        request.decodeSelectedAudio = false;
        return decodeMediaFrames(request, [](auto, auto const&) { return false; }, FfmpegPcmAudioSink{});
    });
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
    auto result = future.get();
    QVERIFY2(result.isSuccess() || result.isStopped(), qPrintable(result.error));
    QVERIFY(!redirected.requests.isEmpty());
    for (auto const& request : redirected.requests) {
        QVERIFY(!request.url.toEncoded().contains("test-token"));
        QVERIFY(!request.headers.contains("test-token"));
    }
}

void JellyfinTest::untrustedTlsIsRejected() {
    if (!QSslSocket::supportsSsl()) {
        QSKIP("Qt TLS backend needed for the isolated TLS server is unavailable");
    }
    UntrustedTlsEdge server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto future = std::async(std::launch::async, [&] {
        FfmpegMediaDecodeRequest request;
        request.video.input = MediaInput(QUrl(QStringLiteral("https://127.0.0.1:%1/stream").arg(server.serverPort())),
                                         QStringLiteral("Untrusted TLS"));
        request.video.firstFrameIdentity = {1, 1, 1};
        request.decodeSelectedAudio = false;
        return decodeMediaFrames(request, [](auto, auto const&) { return false; }, FfmpegPcmAudioSink{});
    });
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
    auto result = future.get();
    QVERIFY(server.accepted > 0);
    QVERIFY(!result.isSuccess() && !result.isCancelled() && !result.error.isEmpty());
}

namespace {
std::mutex diagnosticMutex;
QStringList capturedDiagnostics;
void captureDiagnostics(QtMsgType, QMessageLogContext const&, QString const& message) {
    std::lock_guard lock(diagnosticMutex);
    capturedDiagnostics.push_back(message);
}
} // namespace
void JellyfinTest::failedOpenRedactsCredentials() {
    HttpEdge server;
    server.rejectMedia = true;
    capturedDiagnostics.clear();
    auto previous = qInstallMessageHandler(captureDiagnostics);
    auto restore = qScopeGuard([previous] { qInstallMessageHandler(previous); });
    auto future = std::async(std::launch::async, [&] {
        FfmpegMediaDecodeRequest request;
        request.video.input = MediaInput(server.stream(), QStringLiteral("Failed remote media"));
        request.video.firstFrameIdentity = {1, 1, 1};
        request.decodeSelectedAudio = false;
        return decodeMediaFrames(request, [](auto, auto const&) { return false; }, FfmpegPcmAudioSink{});
    });
    QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
    auto result = future.get();
    QVERIFY(!result.isSuccess());
    // Also exercise diagnostics containing a complete URL and bare credential,
    // since the native 403 diagnostic does not always echo its input URL.
    av_log(nullptr, AV_LOG_ERROR, "Could not open http://localhost/stream?api_key=test-token\n");
    av_log(nullptr, AV_LOG_ERROR, "Authorization=MediaBrowser Token=test-token\n");
    std::lock_guard lock(diagnosticMutex);
    QVERIFY(!capturedDiagnostics.isEmpty());
    QVERIFY(!capturedDiagnostics.join(u'\n').contains(QStringLiteral("test-token")));
    QVERIFY(!result.error.contains(QStringLiteral("test-token")));
}

QTEST_GUILESS_MAIN(JellyfinTest)
#include "tst_Jellyfin.moc"
