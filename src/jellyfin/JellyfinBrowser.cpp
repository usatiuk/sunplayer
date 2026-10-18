#include "jellyfin/JellyfinBrowser.h"
#include "diagnostics/LogCategories.h"

#include <QCryptographicHash>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>

namespace {
void logSettings(QSettings const& settings, char const* stage) {
    qCInfo(sunplayerLogApplication, "event=jellyfin.settings stage=%s status=%d format=%d", stage,
           int(settings.status()), int(settings.format()));
}
} // namespace

JellyfinBrowser::JellyfinBrowser(QString const& settingsPath, QSettings::Format format, bool persistentCredentials,
                                 QObject* parent)
    : QAbstractListModel(parent),
      m_tokens(persistentCredentials
                   ? QStringLiteral("jellyfin-token")
                   : QStringLiteral("jellyfin-test-") +
                         QString::fromLatin1(
                             QCryptographicHash::hash(settingsPath.toUtf8(), QCryptographicHash::Sha256).toHex())),
      m_settings(settingsPath, format) {
    m_settings.setFallbacksEnabled(false);
    logSettings(m_settings, "constructed");
    connect(&m_client, &JellyfinClient::authenticationLost, this, [this] {
        ++m_request;
        m_busy = false;
        m_settings.setValue(QStringLiteral("jellyfin/remember"), false);
        clearLibrary();
        showMessage(tr("Login has expired. Sign in again."));
    });
    if (persistentCredentials) {
        QTimer::singleShot(0, this, &JellyfinBrowser::restore);
    }
}
int JellyfinBrowser::rowCount(QModelIndex const& parent) const { return parent.isValid() ? 0 : m_items.size(); }
QVariant JellyfinBrowser::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    return itemValue(m_items[index.row()], role);
}
QVariant JellyfinBrowser::itemValue(JellyfinItem const& item, int role) {
    switch (role) {
    case ItemId:
        return item.id;
    case Name:
        return item.name;
    case Overview:
        return item.overview;
    case Artwork:
        return item.imageTag.isEmpty()
                   ? QUrl{}
                   : QUrl(QStringLiteral("image://jellyfin/%1/Primary/%2").arg(item.id, item.imageTag));
    case Folder:
        return item.folder;
    case Watched:
        return item.watched;
    case Progress:
        return item.durationMilliseconds > 0
                   ? std::min(1.0, double(item.positionMilliseconds) / item.durationMilliseconds)
                   : 0.0;
    case Playable:
        return item.type == QStringLiteral("Movie") || item.type == QStringLiteral("Episode");
    case Kind:
        return item.type;
    case Year:
        return item.year;
    case Subtitle:
        if (item.type == QStringLiteral("CollectionFolder")) {
            return QString{};
        }
        if (item.type == QStringLiteral("Episode")) {
            QString label = item.seasonNumber >= 0 && item.episodeNumber >= 0
                                ? tr("S%1 · E%2").arg(item.seasonNumber).arg(item.episodeNumber)
                                : QString{};
            if (!item.seriesName.isEmpty()) {
                label = item.seriesName + (label.isEmpty() ? QString{} : QStringLiteral(" · ") + label);
            }
            return label;
        }
        return item.year > 0 ? QString::number(item.year) : QString{};
    case ItemData:
        return itemMap(item);
    case Backdrop:
        return item.backdropTag.isEmpty()
                   ? QUrl{}
                   : QUrl(QStringLiteral("image://jellyfin/%1/Backdrop/%2").arg(item.id, item.backdropTag));
    default:
        return {};
    }
}
namespace {
QHash<int, QByteArray> const itemRoles{{JellyfinBrowser::ItemId, "itemId"},     {JellyfinBrowser::Name, "name"},
                                       {JellyfinBrowser::Overview, "overview"}, {JellyfinBrowser::Artwork, "artwork"},
                                       {JellyfinBrowser::Folder, "folder"},     {JellyfinBrowser::Watched, "watched"},
                                       {JellyfinBrowser::Progress, "progress"}, {JellyfinBrowser::Playable, "playable"},
                                       {JellyfinBrowser::Kind, "kind"},         {JellyfinBrowser::Year, "year"},
                                       {JellyfinBrowser::Subtitle, "subtitle"}, {JellyfinBrowser::Backdrop, "backdrop"},
                                       {JellyfinBrowser::ItemData, "itemData"}};
}
QHash<int, QByteArray> JellyfinBrowser::roleNames() const { return itemRoles; }
QVariantMap JellyfinBrowser::itemMap(JellyfinItem const& item) {
    QVariantMap result;
    for (auto const& [role, name] : itemRoles.asKeyValueRange()) {
        if (role == ItemData) {
            continue;
        }
        result.insert(QString::fromLatin1(name), itemValue(item, role));
    }
    return result;
}
QVariantList JellyfinBrowser::itemMaps(QList<JellyfinItem> const& items) {
    QVariantList result;
    for (auto const& item : items) {
        result.push_back(itemMap(item));
    }
    return result;
}
QVariantMap JellyfinBrowser::currentItem() const { return itemMap(isHome() ? JellyfinItem{} : navigation().back()); }
QVariantList JellyfinBrowser::libraries() const { return itemMaps(m_libraries); }
QVariantList JellyfinBrowser::continueWatching() const { return itemMaps(m_homeItems[ContinueWatching]); }
QVariantList JellyfinBrowser::recentMovies() const { return itemMaps(m_homeItems[RecentMovies]); }
QVariantList JellyfinBrowser::recentShows() const { return itemMaps(m_homeItems[RecentShows]); }
void JellyfinBrowser::clearLibrary() {
    m_openingHome = false;
    m_history = {QList<JellyfinItem>{}};
    m_historyIndex = 0;
    m_libraries.clear();
    for (auto& items : m_homeItems) {
        items.clear();
    }
    setItems({}, false);
}
QString JellyfinBrowser::deviceId() {
    QString id = m_settings.value(QStringLiteral("jellyfin/device")).toString();
    logSettings(m_settings, "device_read");
    if (QUuid(id).isNull()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.setValue(QStringLiteral("jellyfin/device"), id);
        logSettings(m_settings, "device_write");
    }
    return QUuid(id).toString(QUuid::WithoutBraces);
}
void JellyfinBrowser::showMessage(QString message) {
    m_message = std::move(message);
    emit changed();
}
void JellyfinBrowser::setSyncPending(bool pending) {
    m_syncPending = pending;
    emit changed();
}
void JellyfinBrowser::setItems(QList<JellyfinItem> items, bool append) {
    if (append && !items.isEmpty()) {
        beginInsertRows({}, m_items.size(), m_items.size() + items.size() - 1);
        m_items.append(items);
        endInsertRows();
    } else if (!append) {
        beginResetModel();
        m_items = std::move(items);
        endResetModel();
    }
}
void JellyfinBrowser::restore() {
    if (!m_settings.value(QStringLiteral("jellyfin/remember"), false).toBool()) {
        return;
    }
    m_busy = true;
    emit changed();
    quint64 const request = ++m_request;
    m_tokens.read([this, request](QString token, QString error) {
        if (request != m_request) {
            return;
        }
        m_busy = false;
        QUrl server(savedServer());
        QString user = m_settings.value(QStringLiteral("jellyfin/user")).toString();
        if (!error.isEmpty() || token.isEmpty() || user.isEmpty()) {
            m_settings.setValue(QStringLiteral("jellyfin/remember"), false);
            showMessage(error.isEmpty() ? tr("Please sign in again.") : error);
            return;
        }
        if (!m_client.setSession(server, user, token, deviceId())) {
            m_settings.setValue(QStringLiteral("jellyfin/remember"), false);
            showMessage(tr("The saved login is invalid. Please sign in again."));
            return;
        }
        load();
    });
}
void JellyfinBrowser::login(QString address, QString username, QString password, bool remember) {
    if (busy() || authenticated()) {
        return;
    }
    address = address.trimmed();
    if (!address.contains(QStringLiteral("://"))) {
        address.prepend(QStringLiteral("https://"));
    }
    QUrl server(address);
    m_busy = true;
    m_message.clear();
    emit changed();
    quint64 const request = ++m_request;
    logSettings(m_settings, "login_before_write");
    m_settings.setValue(QStringLiteral("jellyfin/remember"), false);
    logSettings(m_settings, "disable_restore");
    m_client.login(server, username, password, deviceId(), [this, request, remember](QString error) {
        if (request != m_request) {
            return;
        }
        if (!error.isEmpty()) {
            m_busy = false;
            showMessage(error);
            return;
        }
        m_settings.setValue(QStringLiteral("jellyfin/server"), m_client.server().toString());
        logSettings(m_settings, "server_write");
        m_settings.setValue(QStringLiteral("jellyfin/user"), m_client.userId());
        logSettings(m_settings, "user_write");
        if (!remember) {
            m_busy = false;
            load();
            return;
        }
        m_tokens.write(m_client.token(), [this, request](QString error) {
            if (request != m_request) {
                return;
            }
            logSettings(m_settings, "token_callback");
            m_settings.setValue(QStringLiteral("jellyfin/remember"), error.isEmpty());
            logSettings(m_settings, "remember_write");
            m_settings.sync();
            logSettings(m_settings, "login_sync");
            if (m_settings.status() != QSettings::NoError) {
                auto const settingsError = tr("Login settings could not be saved. This login may not be remembered.");
                error = error.isEmpty() ? settingsError : error + u'\n' + settingsError;
            }
            m_busy = false;
            m_message = error;
            load();
        });
    });
}
void JellyfinBrowser::logout() {
    if (busy()) {
        return;
    }
    m_busy = true;
    ++m_request;
    // Persist a tombstone before asynchronous deletion so failed deletion never
    // restores a session after a restart.
    m_settings.setValue(QStringLiteral("jellyfin/remember"), false);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        m_message = tr("Automatic login could not be disabled in settings. Secure token deletion is still required.");
    }
    emit changed();
    emit logoutRequested();
}
void JellyfinBrowser::finishLogout() {
    m_client.request("POST", QStringLiteral("Sessions/Logout"), {}, {}, [this](QJsonObject, QString serverError) {
        m_client.clearSession();
        m_tokens.remove([this, serverError](QString error) {
            m_busy = false;
            clearLibrary();
            showMessage(!error.isEmpty() ? error : serverError);
        });
    });
}
void JellyfinBrowser::load(bool append) {
    if (!authenticated()) {
        m_busy = false;
        emit changed();
        return;
    }
    if (isHome()) {
        loadHome();
        return;
    }
    if (!navigation().back().folder) {
        m_busy = true;
        emit changed();
        quint64 const request = ++m_request;
        m_client.item(navigation().back().id, [this, request](JellyfinItem item, QString error) {
            if (request != m_request) {
                return;
            }
            m_busy = false;
            if (error.isEmpty()) {
                navigation().back() = std::move(item);
            } else {
                m_message = error;
            }
            emit changed();
        });
        return;
    }
    m_busy = true;
    emit changed();
    quint64 const request = ++m_request;
    auto done = [this, request, append](QList<JellyfinItem> items, int total, QString error) {
        if (request != m_request) {
            return;
        }
        m_busy = false;
        if (!authenticated()) {
            m_settings.setValue(QStringLiteral("jellyfin/remember"), false);
            clearLibrary();
        }
        if (error.isEmpty()) {
            setItems(std::move(items), append);
            m_total = total;
        } else {
            m_message = error;
        }
        emit changed();
    };
    m_client.items(navigation().back().id, append ? m_items.size() : 0, std::move(done));
}
void JellyfinBrowser::loadHome() {
    m_busy = true;
    m_homePending = 4;
    quint64 const request = ++m_request;
    emit changed();
    auto finished = [this, request](QString const& error) {
        if (request != m_request) {
            return;
        }
        if (!error.isEmpty()) {
            m_message = error;
        }
        Q_ASSERT(m_homePending > 0);
        m_busy = --m_homePending > 0;
        emit changed();
    };
    m_client.libraries([this, request, finished](QList<JellyfinItem> items, QString error) {
        if (request != m_request) {
            return;
        }
        if (error.isEmpty()) {
            m_libraries = std::move(items);
        }
        finished(error);
    });
    auto feed = [this, request, finished](HomeSection section, QList<JellyfinItem> items, QString error) {
        if (request != m_request) {
            return;
        }
        if (error.isEmpty()) {
            m_homeItems[section] = std::move(items);
        }
        finished(error.isEmpty() ? error : tr("Home shelf could not be refreshed. %1").arg(error));
    };
    m_client.resume(
        [feed](QList<JellyfinItem> items, QString error) { feed(ContinueWatching, std::move(items), error); });
    m_client.recent(false,
                    [feed](QList<JellyfinItem> items, QString error) { feed(RecentMovies, std::move(items), error); });
    m_client.recent(true,
                    [feed](QList<JellyfinItem> items, QString error) { feed(RecentShows, std::move(items), error); });
}
void JellyfinBrowser::openItem(JellyfinItem item) {
    if (!item.folder && item.type != QStringLiteral("Movie")) {
        emit playRequested(item, false);
        return;
    }
    auto path = navigation();
    path.push_back(std::move(item));
    visit(std::move(path));
}
void JellyfinBrowser::loadLocation() {
    m_message.clear();
    setItems({}, false);
    m_total = 0;
    load();
}
void JellyfinBrowser::visit(QList<JellyfinItem> path) {
    if ((path.isEmpty() && isHome()) || (!path.isEmpty() && !isHome() && path.back().id == navigation().back().id)) {
        return;
    }
    m_history.resize(m_historyIndex + 1);
    m_history.push_back(std::move(path));
    ++m_historyIndex;
    loadLocation();
}
void JellyfinBrowser::home() {
    if (!busy()) {
        visit({});
    }
}
void JellyfinBrowser::selectLibrary(int row) {
    if (!busy() && row >= 0 && row < m_libraries.size()) {
        visit({m_libraries[row]});
    }
}
void JellyfinBrowser::openHomeItem(int section, int row) {
    if (busy() || section < 0 || section >= int(m_homeItems.size()) || row < 0 || row >= m_homeItems[section].size()) {
        return;
    }
    auto const item = m_homeItems[section][row];
    m_openingHome = true;
    m_busy = true;
    m_message.clear();
    auto const request = ++m_request;
    emit changed();
    m_client.ancestors(item.id, [this, request, item](QList<JellyfinItem> ancestors, QString error) {
        if (request != m_request) {
            return;
        }
        m_openingHome = false;
        m_busy = false;
        if (!error.isEmpty()) {
            showMessage(error);
            return;
        }
        // Ignore server root folders above the user's actual library.
        auto library = std::find_if(ancestors.cbegin(), ancestors.cend(), [this](JellyfinItem const& parent) {
            return std::any_of(m_libraries.cbegin(), m_libraries.cend(),
                               [&parent](JellyfinItem const& value) { return value.id == parent.id; });
        });
        QList<JellyfinItem> path;
        if (library != ancestors.cend()) {
            path = QList<JellyfinItem>(library, ancestors.cend());
        } else {
            // Grouped UserViews can have different IDs from physical library
            // ancestors. Keep the server's media hierarchy without guessing a tab.
            for (auto const& parent : ancestors) {
                if (parent.type == QStringLiteral("CollectionFolder") || parent.type == QStringLiteral("Series") ||
                    parent.type == QStringLiteral("Season")) {
                    path.push_back(parent);
                }
            }
        }
        if (item.folder || item.type == QStringLiteral("Movie")) {
            path.push_back(item);
            visit(std::move(path));
        } else {
            visit(std::move(path));
            emit playRequested(item, false);
        }
    });
}
void JellyfinBrowser::cancelHomeOpening() {
    if (m_openingHome) {
        ++m_request;
        m_openingHome = false;
        m_busy = false;
        emit changed();
    }
}
void JellyfinBrowser::playCurrent(bool fromStart) {
    if (!busy() && !isHome() && !navigation().back().folder) {
        emit playRequested(navigation().back(), fromStart);
    }
}
void JellyfinBrowser::setCurrentWatched(bool watched) {
    if (!busy() && !isHome() && !navigation().back().folder) {
        emit watchedRequested(navigation().back().id, watched);
    }
}

void JellyfinBrowser::enter(int row) {
    if (busy() || row < 0 || row >= m_items.size()) {
        return;
    }
    openItem(m_items[row]);
}
void JellyfinBrowser::up() {
    if (!busy() && canGoUp()) {
        auto path = navigation();
        path.pop_back();
        visit(std::move(path));
    }
}
void JellyfinBrowser::back() {
    if (!busy() && canGoBack()) {
        --m_historyIndex;
        loadLocation();
    }
}
void JellyfinBrowser::forward() {
    if (!busy() && canGoForward()) {
        ++m_historyIndex;
        loadLocation();
    }
}
void JellyfinBrowser::loadMore() {
    if (!busy() && hasMore()) {
        load(true);
    }
}
void JellyfinBrowser::refresh() {
    if (!m_busy && authenticated()) {
        load();
    }
}
void JellyfinBrowser::retry() {
    if (!busy()) {
        m_message.clear();
        load();
    }
}
void JellyfinBrowser::play(int row, bool fromStart) {
    if (busy() || row < 0 || row >= m_items.size() || m_items[row].folder) {
        return;
    }
    emit playRequested(m_items[row], fromStart);
}
void JellyfinBrowser::setWatched(int row, bool watched) {
    if (busy() || row < 0 || row >= m_items.size()) {
        return;
    }
    emit watchedRequested(m_items[row].id, watched);
}
void JellyfinBrowser::applyWatched(QString const& id, bool watched) {
    // A deferred mutation supersedes the refresh started by its stop report.
    ++m_request;
    m_busy = true;
    emit changed();
    m_client.setWatched(id, watched, [this](QString error) {
        m_busy = false;
        if (!error.isEmpty()) {
            showMessage(error);
        } else {
            m_message.clear();
            load();
        }
    });
}
