#pragma once

#include <vector>

#include <QAbstractListModel>

#include "subtitles/SubtitleTypes.h"

class SubtitleTrackModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        StreamIndexRole,
        SelectedRole,
        EnabledRole,
    };

    explicit SubtitleTrackModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(
        const QModelIndex &index,
        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTracks(
        std::vector<SubtitleTrackDescriptor> tracks,
        int selectedStreamIndex);
    void setSelectedStreamIndex(int streamIndex);
    int selectedStreamIndex() const;
    bool canSelect(int streamIndex) const;

private:
    struct Entry {
        QString label;
        int streamIndex = -1;
        bool enabled = true;
    };

    std::vector<Entry> m_entries;
    int m_selectedStreamIndex = -1;
};
