#include "platform/NativeMediaControls.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QThread>
#include <QVariantMap>

#include "diagnostics/LogCategories.h"
#include "playback/MediaSession.h"

namespace {
constexpr auto objectPath = "/org/mpris/MediaPlayer2";
constexpr auto rootInterface = "org.mpris.MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";

// Virtual dispatch keeps protocol validation and D-Bus errors at the external
// boundary. Calls execute on this object's (and the session's) Qt thread.
class LinuxNativeMediaControls final : public QDBusVirtualObject, public NativeMediaControls {
  public:
    explicit LinuxNativeMediaControls(MediaSession& session)
        : m_session(session),
          m_connectionName(QStringLiteral("sunplayer-mpris-%1").arg(QCoreApplication::applicationPid())),
          m_service(
              QStringLiteral("org.mpris.MediaPlayer2.sunplayer.instance%1").arg(QCoreApplication::applicationPid())) {
        Q_ASSERT(QThread::currentThread() == session.thread());
        connect(&session, &MediaSession::sessionChanged, this, [this] { publish(true); });
        connect(&session, &MediaSession::timelineChanged, this, [this] { publish(); });
        connect(&session, &MediaSession::volumeChanged, this, [this] { publish(); });
        connect(&session, &MediaSession::mutedChanged, this, [this] { publish(); });
        connect(&session, &MediaSession::positionJumped, this, [this](qlonglong position) { seeked(position * 1000); });
        publish(true);
    }

    ~LinuxNativeMediaControls() override {
        if (m_registered) {
            auto bus = QDBusConnection(m_connectionName);
            bus.unregisterObject(QString::fromLatin1(objectPath));
            bus.unregisterService(m_service);
        }
        QDBusConnection::disconnectFromBus(m_connectionName);
    }

    QString introspect(QString const&) const override {
        return QStringLiteral(R"xml(
<interface name="org.mpris.MediaPlayer2">
 <method name="Raise"/><method name="Quit"/>
 <property name="CanQuit" type="b" access="read"/>
 <property name="CanRaise" type="b" access="read"/>
 <property name="HasTrackList" type="b" access="read"/>
 <property name="Identity" type="s" access="read"/>
 <property name="DesktopEntry" type="s" access="read"/>
 <property name="SupportedUriSchemes" type="as" access="read"/>
 <property name="SupportedMimeTypes" type="as" access="read"/>
</interface>
<interface name="org.mpris.MediaPlayer2.Player">
 <method name="Next"/><method name="Previous"/><method name="Pause"/>
 <method name="PlayPause"/><method name="Stop"/><method name="Play"/>
 <method name="Seek"><arg name="Offset" type="x" direction="in"/></method>
 <method name="SetPosition"><arg name="TrackId" type="o" direction="in"/><arg name="Position" type="x" direction="in"/></method>
 <signal name="Seeked"><arg name="Position" type="x"/></signal>
 <property name="PlaybackStatus" type="s" access="read"/>
 <property name="Rate" type="d" access="readwrite"/>
 <property name="Metadata" type="a{sv}" access="read"/>
 <property name="Volume" type="d" access="readwrite"/>
 <property name="Position" type="x" access="read">
  <annotation name="org.freedesktop.DBus.Property.EmitsChangedSignal" value="false"/>
 </property>
 <property name="MinimumRate" type="d" access="read"/>
 <property name="MaximumRate" type="d" access="read"/>
 <property name="CanGoNext" type="b" access="read"/>
 <property name="CanGoPrevious" type="b" access="read"/>
 <property name="CanPlay" type="b" access="read"/>
 <property name="CanPause" type="b" access="read"/>
 <property name="CanSeek" type="b" access="read"/>
 <property name="CanControl" type="b" access="read">
  <annotation name="org.freedesktop.DBus.Property.EmitsChangedSignal" value="false"/>
 </property>
</interface>
<interface name="org.freedesktop.DBus.Properties">
 <method name="Get"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
 <method name="Set"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/></method>
 <method name="GetAll"><arg type="s" direction="in"/><arg type="a{sv}" direction="out"/></method>
 <signal name="PropertiesChanged"><arg type="s"/><arg type="a{sv}"/><arg type="as"/></signal>
</interface>)xml");
    }

    bool handleMessage(QDBusMessage const& message, QDBusConnection const& connection) override {
        Q_ASSERT(QThread::currentThread() == m_session.thread());
        auto reply = [&](QList<QVariant> const& arguments = {}) {
            return connection.send(message.createReply(arguments));
        };
        auto error = [&](char const* name, QString const& description) {
            return connection.send(message.createErrorReply(QString::fromLatin1(name), description));
        };
        auto invalid = [&] {
            return error("org.freedesktop.DBus.Error.InvalidArgs", QStringLiteral("Invalid method arguments"));
        };
        auto const& arguments = message.arguments();
        QString const member = message.member();
        if (message.interface() == u"org.freedesktop.DBus.Introspectable" && member == u"Introspect") {
            return message.signature().isEmpty()
                       ? reply({QStringLiteral("<node>") + introspect(message.path()) + QStringLiteral("</node>")})
                       : invalid();
        }
        if (message.interface() == QLatin1String(propertiesInterface)) {
            if ((member == u"Get" && message.signature() != u"ss") ||
                (member == u"GetAll" && message.signature() != u"s") ||
                (member == u"Set" && message.signature() != u"ssv")) {
                return invalid();
            }
            if (member != u"Get" && member != u"GetAll" && member != u"Set") {
                return error("org.freedesktop.DBus.Error.UnknownMethod", member);
            }
            QString const interface = arguments[0].toString();
            if (interface != QLatin1String(rootInterface) && interface != QLatin1String(playerInterface)) {
                return error("org.freedesktop.DBus.Error.UnknownInterface", interface);
            }
            QVariantMap values = properties(interface);
            if (member == u"GetAll") {
                return reply({values});
            }
            QString const property = arguments[1].toString();
            if (!values.contains(property)) {
                return error("org.freedesktop.DBus.Error.UnknownProperty", property);
            }
            if (member == u"Get") {
                return reply({QVariant::fromValue(QDBusVariant(values[property]))});
            }
            if (interface != QLatin1String(playerInterface) || (property != u"Volume" && property != u"Rate")) {
                return error("org.freedesktop.DBus.Error.PropertyReadOnly", property);
            }
            QVariant const value = qvariant_cast<QDBusVariant>(arguments[2]).variant();
            if (value.metaType() != QMetaType::fromType<double>() || !std::isfinite(value.toDouble())) {
                return invalid();
            }
            if (property == u"Volume") {
                m_session.setVolume(std::clamp(value.toDouble(), 0.0, 1.0));
                m_session.setMuted(false);
            } else if (value.toDouble() == 0.0) {
                m_session.pause();
            } else if (value.toDouble() != 1.0) {
                return error("org.freedesktop.DBus.Error.NotSupported",
                             QStringLiteral("Only normal playback rate is supported"));
            }
            return reply();
        }
        if (message.interface() == QLatin1String(rootInterface) && (member == u"Raise" || member == u"Quit")) {
            return message.signature().isEmpty() ? reply() : invalid();
        }
        if (message.interface() != QLatin1String(playerInterface)) {
            return error("org.freedesktop.DBus.Error.UnknownMethod", member);
        }
        if (member == u"Seek") {
            if (message.signature() != u"x") {
                return invalid();
            }
            if (m_session.seekable()) {
                qlonglong const offset = arguments[0].toLongLong();
                qlonglong const current = position();
                qlonglong const duration = m_session.durationMilliseconds() * 1000;
                // Compare before addition: even hostile INT64 offsets cannot overflow.
                if (offset <= duration - current) {
                    m_session.seekToMilliseconds(offset < -current ? 0 : (current + offset) / 1000);
                }
                // An over-end seek means Next; CanGoNext is false, so it does nothing.
            }
            return reply();
        }
        if (member == u"SetPosition") {
            if (message.signature() != u"ox") {
                return invalid();
            }
            qlonglong const target = arguments[1].toLongLong();
            if (m_session.seekable() && qvariant_cast<QDBusObjectPath>(arguments[0]) == itemPath() && target >= 0 &&
                target <= m_session.durationMilliseconds() * 1000) {
                m_session.seekToMilliseconds(target / 1000);
            }
            return reply();
        }
        if (!message.signature().isEmpty()) {
            return invalid();
        }
        bool const playable = canPlayPause();
        if (member == u"Play") {
            if (playable) {
                m_session.play();
            }
        } else if (member == u"Pause") {
            if (playable) {
                m_session.pause();
            }
        } else if (member == u"PlayPause") {
            if (!playable) {
                return error("org.freedesktop.DBus.Error.NotSupported", QStringLiteral("Playback is unavailable"));
            }
            m_session.togglePlayback();
        } else if (member == u"Stop") {
            m_session.stop();
        } else if (member != u"Next" && member != u"Previous") {
            return error("org.freedesktop.DBus.Error.UnknownMethod", member);
        }
        return reply();
    }

  private:
    bool hasItem() const {
        return m_session.mediaItemId() != 0 && m_session.state() != MediaSession::State::Error &&
               m_session.state() != MediaSession::State::Empty;
    }
    bool canPlayPause() const {
        return hasItem() && (m_session.state() == MediaSession::State::Ready || m_session.seeking());
    }
    QDBusObjectPath itemPath() const {
        return QDBusObjectPath(QStringLiteral("/com/sunplayer/item/%1").arg(m_session.mediaItemId()));
    }
    qlonglong position() const { return hasItem() ? m_session.positionMilliseconds() * 1000 : 0; }
    QVariantMap properties(QString const& interface) const {
        if (interface == QLatin1String(rootInterface)) {
            return {{QStringLiteral("CanQuit"), false},
                    {QStringLiteral("CanRaise"), false},
                    {QStringLiteral("HasTrackList"), false},
                    {QStringLiteral("Identity"), QStringLiteral("SunPlayer")},
                    {QStringLiteral("DesktopEntry"), QStringLiteral("sunplayer")},
                    {QStringLiteral("SupportedUriSchemes"), QStringList{}},
                    {QStringLiteral("SupportedMimeTypes"), QStringList{}}};
        }
        QVariantMap metadata;
        if (hasItem()) {
            metadata.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(itemPath()));
            metadata.insert(QStringLiteral("xesam:title"), m_session.displayName());
            if (m_session.durationMilliseconds() > 0) {
                metadata.insert(QStringLiteral("mpris:length"), m_session.durationMilliseconds() * 1000);
            }
        }
        QString const status = !hasItem() || m_session.stopped() || m_session.ended() ? QStringLiteral("Stopped")
                               : m_session.playRequested()                            ? QStringLiteral("Playing")
                                                                                      : QStringLiteral("Paused");
        return {{QStringLiteral("PlaybackStatus"), status},
                {QStringLiteral("Rate"), 1.0},
                {QStringLiteral("MinimumRate"), 1.0},
                {QStringLiteral("MaximumRate"), 1.0},
                {QStringLiteral("Metadata"), metadata},
                {QStringLiteral("Volume"), m_session.muted() ? 0.0 : m_session.volume()},
                {QStringLiteral("Position"), position()},
                {QStringLiteral("CanGoNext"), false},
                {QStringLiteral("CanGoPrevious"), false},
                {QStringLiteral("CanPlay"), canPlayPause()},
                {QStringLiteral("CanPause"), canPlayPause()},
                {QStringLiteral("CanSeek"), m_session.seekable()},
                {QStringLiteral("CanControl"), true}};
    }
    void publish(bool retryBus = false) {
        auto bus = QDBusConnection(m_connectionName);
        if (!bus.isConnected()) {
            if (!retryBus) {
                return;
            }
            if (m_registered) {
                bus.unregisterObject(QString::fromLatin1(objectPath));
            }
            m_registered = false;
            QDBusConnection::disconnectFromBus(m_connectionName);
            bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
            if (!bus.isConnected()) {
                if (!m_busUnavailable) {
                    qCWarning(sunplayerLogPlatform, "MPRIS unavailable: no session D-Bus; retrying on session changes");
                }
                m_busUnavailable = true;
                return;
            }
        }
        if (!m_registered) {
            if (!retryBus) {
                return;
            }
            if (!bus.registerVirtualObject(QString::fromLatin1(objectPath), this) || !bus.registerService(m_service)) {
                QDBusError const error = bus.lastError();
                // Transport loss and bus policy are external. Invalid names,
                // signatures or duplicate object/service ownership are defects.
                switch (error.type()) {
                case QDBusError::Disconnected:
                case QDBusError::NoServer:
                case QDBusError::NoNetwork:
                case QDBusError::NoReply:
                case QDBusError::Timeout:
                case QDBusError::TimedOut:
                case QDBusError::AccessDenied:
                    bus.unregisterObject(QString::fromLatin1(objectPath));
                    QDBusConnection::disconnectFromBus(m_connectionName);
                    if (!m_busUnavailable) {
                        qCWarning(sunplayerLogPlatform, "MPRIS unavailable: %s; retrying on session changes",
                                  qPrintable(error.message()));
                    }
                    m_busUnavailable = true;
                    return;
                default:
                    qFatal("Cannot register MPRIS: %s", qPrintable(error.message()));
                }
            }
            m_registered = true;
            m_busUnavailable = false;
            m_published.clear();
        }
        QVariantMap current = properties(QString::fromLatin1(playerInterface));
        current.remove(QStringLiteral("Position"));
        current.remove(QStringLiteral("CanControl"));
        QVariantMap changed;
        for (auto it = current.cbegin(); it != current.cend(); ++it) {
            if (m_published.value(it.key()) != it.value()) {
                changed.insert(it.key(), it.value());
            }
        }
        m_published = current;
        if (!changed.isEmpty()) {
            auto signal =
                QDBusMessage::createSignal(QString::fromLatin1(objectPath), QString::fromLatin1(propertiesInterface),
                                           QStringLiteral("PropertiesChanged"));
            signal.setArguments({QString::fromLatin1(playerInterface), changed, QStringList{}});
            send(signal);
        }
        bool const advancing = hasItem() && m_session.mediaClockSnapshot().advancing;
        if (advancing != m_advancing && hasItem() && m_session.playRequested()) {
            seeked(position());
        }
        m_advancing = advancing;
    }
    void seeked(qlonglong positionMicroseconds) {
        if (!m_registered || !hasItem()) {
            return;
        }
        auto signal = QDBusMessage::createSignal(QString::fromLatin1(objectPath), QString::fromLatin1(playerInterface),
                                                 QStringLiteral("Seeked"));
        signal.setArguments({positionMicroseconds});
        send(signal);
    }
    void send(QDBusMessage const& message) {
        auto bus = QDBusConnection(m_connectionName);
        if (!bus.send(message)) {
            qCWarning(sunplayerLogPlatform, "Could not publish MPRIS update: %s",
                      qPrintable(bus.lastError().message()));
        }
    }

    MediaSession& m_session;
    QString const m_connectionName;
    QString const m_service;
    QVariantMap m_published;
    bool m_registered = false;
    bool m_busUnavailable = false;
    bool m_advancing = false;
};
} // namespace

std::unique_ptr<NativeMediaControls> createNativeMediaControls(MediaSession& session, QWindow&) {
    return std::make_unique<LinuxNativeMediaControls>(session);
}
