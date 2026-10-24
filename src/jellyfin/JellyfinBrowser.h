#pragma once

#include "jellyfin/JellyfinClient.h"
#include "jellyfin/TokenStore.h"
#include "settings/SettingsStore.h"
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <array>

class JellyfinBrowser final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool isHome READ isHome NOTIFY changed)
    Q_PROPERTY(QString activeLibraryId READ activeLibraryId NOTIFY changed)
    Q_PROPERTY(QVariantMap currentItem READ currentItem NOTIFY changed)
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY changed)
    Q_PROPERTY(QVariantList continueWatching READ continueWatching NOTIFY changed)
    Q_PROPERTY(QVariantList recentMovies READ recentMovies NOTIFY changed)
    Q_PROPERTY(QVariantList recentShows READ recentShows NOTIFY changed)
    Q_PROPERTY(QString savedServer READ savedServer CONSTANT)
  public:
    enum Role {
        ItemId = Qt::UserRole + 1,
        Name,
        Overview,
        Artwork,
        Folder,
        Watched,
        Progress,
        Playable,
        Kind,
        Year,
        Subtitle,
        Backdrop,
        SeriesName,
        ItemData
    };
    enum HomeSection { ContinueWatching, RecentMovies, RecentShows };
    Q_ENUM(HomeSection)
    JellyfinBrowser(SettingsStore& settings, bool persistentCredentials, QObject* parent = nullptr);
    int rowCount(QModelIndex const& parent = {}) const override;
    QVariant data(QModelIndex const& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool authenticated() const { return m_client.authenticated(); }
    bool busy() const { return m_busy || m_syncPending; }
    QString message() const { return m_message; }
    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoUp() const { return navigation().size() > 1; }
    bool canGoForward() const { return m_historyIndex + 1 < m_history.size(); }
    bool hasMore() const { return !isHome() && navigation().back().folder && m_items.size() < m_total; }
    bool isHome() const { return navigation().isEmpty(); }
    QString activeLibraryId() const { return isHome() ? QString{} : navigation().front().id; }
    QVariantMap currentItem() const;
    QVariantList libraries() const;
    QVariantList continueWatching() const;
    QVariantList recentMovies() const;
    QVariantList recentShows() const;
    QString savedServer() const { return m_settings.value(QStringLiteral("jellyfin/server")).toString(); }
    JellyfinClient& client() { return m_client; }
    void setSyncPending(bool pending);
    void showMessage(QString message);
    void refresh();
    void cancelHomeOpening();
    Q_INVOKABLE void login(QString server, QString username, QString password, bool remember);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void enter(int row);
    Q_INVOKABLE void home();
    Q_INVOKABLE void selectLibrary(int row);
    Q_INVOKABLE void openHomeItem(int section, int row);
    Q_INVOKABLE void playCurrent(bool fromStart = false);
    Q_INVOKABLE void setCurrentWatched(bool watched);
    Q_INVOKABLE void back();
    Q_INVOKABLE void up();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void play(int row, bool fromStart = false);
    Q_INVOKABLE void setWatched(int row, bool watched);
    Q_INVOKABLE void retry();
    void finishLogout();
    void applyWatched(QString const& id, bool watched);
  signals:
    void changed();
    void playRequested(JellyfinItem item, bool fromStart);
    void logoutRequested();
    void watchedRequested(QString id, bool watched);

  private:
    void load(bool append = false);
    void loadHome();
    void openItem(JellyfinItem item);
    void clearLibrary();
    void visit(QList<JellyfinItem> path);
    void loadLocation();
    QList<JellyfinItem>& navigation() { return m_history[m_historyIndex]; }
    QList<JellyfinItem> const& navigation() const { return m_history[m_historyIndex]; }
    static QVariant itemValue(JellyfinItem const& item, int role);
    static QVariantMap itemMap(JellyfinItem const& item);
    static QVariantList itemMaps(QList<JellyfinItem> const& items);
    void restore();
    void setItems(QList<JellyfinItem> items, bool append);
    QString deviceId();
    JellyfinClient m_client;
    TokenStore m_tokens;
    SettingsStore& m_settings;
    QList<JellyfinItem> m_items;
    QList<QList<JellyfinItem>> m_history{QList<JellyfinItem>{}};
    int m_historyIndex = 0;
    QList<JellyfinItem> m_libraries;
    std::array<QList<JellyfinItem>, 3> m_homeItems;
    int m_homePending = 0;
    int m_total = 0;
    quint64 m_request = 0;
    bool m_busy = false;
    bool m_openingHome = false;
    bool m_syncPending = false;
    QString m_message;
};
