#include <limits>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QEventLoop>
#include <QWindow>
#include <QtTest>

#include "audio/ControlledAudioSink.h"
#include "platform/NativeMediaControls.h"
#include "playback/MediaSession.h"

namespace {
constexpr auto path = "/org/mpris/MediaPlayer2";
constexpr auto player = "org.mpris.MediaPlayer2.Player";
constexpr auto properties = "org.freedesktop.DBus.Properties";
} // namespace

class LinuxNativeMediaControlsTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("mpris-test-client"));
        QVERIFY2(bus.isConnected(), qPrintable(bus.lastError().message()));
    }
    void cleanupTestCase() { QDBusConnection::disconnectFromBus(QStringLiteral("mpris-test-client")); }
    void protocolAndSession() {
        MediaSession session(VideoTargetReadback::Disabled);
        QWindow window;
        auto controls = createNativeMediaControls(session, window);
        auto bus = QDBusConnection(QStringLiteral("mpris-test-client"));
        QVERIFY(bus.connect(service(), QString::fromLatin1(path), QString::fromLatin1(player), QStringLiteral("Seeked"),
                            this, SLOT(seeked(qlonglong))));
        QVERIFY(bus.connect(service(), QString::fromLatin1(path), QString::fromLatin1(properties),
                            QStringLiteral("PropertiesChanged"), this,
                            SLOT(changed(QString, QVariantMap, QStringList))));

        auto reply = call(QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QVERIFY(reply.arguments()[0].toString().contains(QStringLiteral("SetPosition")));
        QCOMPARE(get(QStringLiteral("CanControl")).toBool(), true);
        QCOMPARE(get(QStringLiteral("CanPlay")).toBool(), false);
        QCOMPARE(get(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Stopped"));
        QCOMPARE(get(QStringLiteral("Rate")).metaType(), QMetaType::fromType<double>());
        QCOMPARE(get(QStringLiteral("Position")).metaType(), QMetaType::fromType<qlonglong>());
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("Seek"), {42}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("PlayPause")).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(
            call(QString::fromLatin1(properties), QStringLiteral("GetAll"), {QString::fromLatin1(player)}).signature(),
            QStringLiteral("a{sv}"));
        QCOMPARE(
            call(QString::fromLatin1(properties), QStringLiteral("GetAll"), {QStringLiteral("org.mpris.MediaPlayer2")})
                .signature(),
            QStringLiteral("a{sv}"));

        session.openMedia(
            QUrl::fromLocalFile(QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-playback.mkv")));
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
        session.pause();
        QVariantMap const metadata = qdbus_cast<QVariantMap>(get(QStringLiteral("Metadata")));
        auto const id = qvariant_cast<QDBusObjectPath>(metadata.value(QStringLiteral("mpris:trackid")));
        QVERIFY(!id.path().isEmpty());
        QVERIFY(!metadata.contains(QStringLiteral("xesam:url")));
        QCOMPARE(get(QStringLiteral("CanSeek")).toBool(), true);
        QCOMPARE(get(QStringLiteral("CanGoNext")).toBool(), false);
        QCOMPARE(get(QStringLiteral("CanGoPrevious")).toBool(), false);
        qlonglong const duration = metadata.value(QStringLiteral("mpris:length")).toLongLong();
        QVERIFY(duration > 500000);

        auto const generation = session.playbackGeneration();
        auto setPosition = [&](QDBusObjectPath const& item, qlonglong target) {
            return call(QString::fromLatin1(player), QStringLiteral("SetPosition"),
                        {QVariant::fromValue(item), target});
        };
        QCOMPARE(setPosition(id, -1).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(setPosition(id, duration + 1).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(setPosition(QDBusObjectPath(QStringLiteral("/stale")), 500000).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(
            call(QString::fromLatin1(player), QStringLiteral("Seek"), {std::numeric_limits<qlonglong>::max()}).type(),
            QDBusMessage::ReplyMessage);
        QCOMPARE(session.playbackGeneration(), generation);
        QCOMPARE(setPosition(id, 500000).type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
        QCOMPARE(session.positionMilliseconds(), 500);
        QCOMPARE(qvariant_cast<QDBusObjectPath>(
                     qdbus_cast<QVariantMap>(get(QStringLiteral("Metadata"))).value(QStringLiteral("mpris:trackid"))),
                 id);
        QTRY_VERIFY(m_seeked.contains(500000));
        QCOMPARE(
            call(QString::fromLatin1(player), QStringLiteral("Seek"), {std::numeric_limits<qlonglong>::min()}).type(),
            QDBusMessage::ReplyMessage);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
        QCOMPARE(session.positionMilliseconds(), 0);

        session.setMuted(true);
        QCOMPARE(get(QStringLiteral("Volume")).toDouble(), 0.0);
        QCOMPARE(set(QStringLiteral("Volume"), 0.4).type(), QDBusMessage::ReplyMessage);
        QVERIFY(!session.muted());
        QCOMPARE(session.volume(), 0.4);
        QCOMPARE(set(QStringLiteral("Volume"), -1.0).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(session.volume(), 0.0);
        QCOMPARE(set(QStringLiteral("Volume"), std::numeric_limits<double>::quiet_NaN()).type(),
                 QDBusMessage::ErrorMessage);
        QCOMPARE(set(QStringLiteral("Volume"), 1).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(set(QStringLiteral("Rate"), 2.0).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("PlayPause")).type(), QDBusMessage::ReplyMessage);
        QVERIFY(session.playRequested());
        QCOMPARE(set(QStringLiteral("Rate"), 0.0).type(), QDBusMessage::ReplyMessage);
        QVERIFY(!session.playRequested());
        QCOMPARE(get(QStringLiteral("Rate")).toDouble(), 1.0);

        session.seekToMilliseconds(500);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("Stop")).type(), QDBusMessage::ReplyMessage);
        QVERIFY(session.stopped());
        QCOMPARE(session.positionMilliseconds(), 500);
        QCOMPARE(get(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Stopped"));
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("Play")).type(), QDBusMessage::ReplyMessage);
        session.pause();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
        QVERIFY(session.positionMilliseconds() < 500);

        // A real replacement must reject an otherwise valid request for the old item.
        session.openMedia(QUrl::fromLocalFile(QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1.mkv")));
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
        session.pause();
        QVERIFY(qvariant_cast<QDBusObjectPath>(
                    qdbus_cast<QVariantMap>(get(QStringLiteral("Metadata"))).value(QStringLiteral("mpris:trackid"))) !=
                id);
        auto const replacementGeneration = session.playbackGeneration();
        QCOMPARE(setPosition(id, 0).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(session.playbackGeneration(), replacementGeneration);

        session.cancel();
        QVERIFY(qdbus_cast<QVariantMap>(get(QStringLiteral("Metadata"))).isEmpty());
        QTRY_VERIFY(m_metadataCleared);
        QVERIFY(!m_changedPosition);
        QVERIFY(!m_changedCanControl);
        controls.reset();
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("Play")).type(), QDBusMessage::ErrorMessage);
    }
    void bufferingUsesIntentForToggle() {
        auto sink = std::make_shared<ControlledAudioSink>(4096);
        MediaSession session(
            VideoTargetReadback::Disabled,
            [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
               FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
               FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
                return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
            },
            sink);
        QWindow window;
        auto controls = createNativeMediaControls(session, window);
        session.openMedia(
            QUrl::fromLocalFile(QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-flac-sync.mkv")));
        QElapsedTimer startup;
        startup.start();
        while ((session.state() != MediaSession::State::Ready || !sink->snapshot().valid) && startup.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            auto const rendered = sink->render(257);
            if (rendered.frames) {
                sink->advancePresentedFrames(rendered.frames);
            }
            session.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
            QThread::yieldCurrentThread();
        }
        QCOMPARE(session.state(), MediaSession::State::Ready);
        QVERIFY(sink->snapshot().valid);
        QTRY_VERIFY_WITH_TIMEOUT(sink->bufferedFrames() != 0, 1000);
        auto const holdStarted = std::chrono::steady_clock::now();
        auto const stalled = sink->advanceOutput(4096 + 24000);
        QVERIFY(stalled.mediaFrames != 0);
        QVERIFY(stalled.holdFrames >= 24000);
        session.videoSource().prepareForPresentation(holdStarted);
        session.videoSource().prepareForPresentation(holdStarted + std::chrono::milliseconds(600));
        QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::Buffering);
        QVERIFY(session.playRequested());
        QVERIFY(!session.mediaClockSnapshot().advancing);
        QCOMPARE(get(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Playing"));
        auto const heldPosition = get(QStringLiteral("Position")).toLongLong();
        QCOMPARE(heldPosition, session.positionMilliseconds() * 1000);
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("PlayPause")).type(), QDBusMessage::ReplyMessage);
        QVERIFY(!session.playRequested());
        QCOMPARE(get(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Paused"));
        QCOMPARE(call(QString::fromLatin1(player), QStringLiteral("PlayPause")).type(), QDBusMessage::ReplyMessage);
        QVERIFY(session.playRequested());
        QCOMPARE(get(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Playing"));
        QTRY_VERIFY_WITH_TIMEOUT(sink->bufferedFrames() != 0, 1000);
        auto const resumed = sink->render(257);
        QVERIFY(resumed.frames != 0);
        // Keep an interval in flight so the device reports active advancement.
        sink->advancePresentedFrames(resumed.frames / 2);
        session.videoSource().prepareForPresentation(holdStarted + std::chrono::milliseconds(700));
        QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::None);
        QVERIFY(session.mediaClockSnapshot().advancing);
        QVERIFY(get(QStringLiteral("Position")).toLongLong() > heldPosition);
    }
    void seeked(qlonglong position) { m_seeked.append(position); }
    void changed(QString, QVariantMap values, QStringList) {
        m_changedPosition |= values.contains(QStringLiteral("Position"));
        m_changedCanControl |= values.contains(QStringLiteral("CanControl"));
        if (values.contains(QStringLiteral("Metadata")) &&
            qdbus_cast<QVariantMap>(values.value(QStringLiteral("Metadata"))).isEmpty()) {
            m_metadataCleared = true;
        }
    }

  private:
    QString service() const {
        return QStringLiteral("org.mpris.MediaPlayer2.sunplayer.instance%1").arg(QCoreApplication::applicationPid());
    }
    QDBusMessage call(QString const& interface, QString const& member, QList<QVariant> const& args = {}) {
        auto message = QDBusMessage::createMethodCall(service(), QString::fromLatin1(path), interface, member);
        message.setArguments(args);
        auto bus = QDBusConnection(QStringLiteral("mpris-test-client"));
        QDBusPendingCallWatcher watcher(bus.asyncCall(message, 3000));
        QEventLoop loop;
        connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        if (!watcher.isFinished()) {
            loop.exec();
        }
        return watcher.reply();
    }
    QVariant get(QString const& property) {
        auto reply =
            call(QString::fromLatin1(properties), QStringLiteral("Get"), {QString::fromLatin1(player), property});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1) {
            return {};
        }
        return qvariant_cast<QDBusVariant>(reply.arguments()[0]).variant();
    }
    QDBusMessage set(QString const& property, QVariant const& value) {
        return call(QString::fromLatin1(properties), QStringLiteral("Set"),
                    {QString::fromLatin1(player), property, QVariant::fromValue(QDBusVariant(value))});
    }
    QList<qlonglong> m_seeked;
    bool m_changedPosition = false;
    bool m_changedCanControl = false;
    bool m_metadataCleared = false;
};

QTEST_MAIN(LinuxNativeMediaControlsTest)
#include "tst_LinuxNativeMediaControls.moc"
