#pragma once

#include <vector>

#include <QAbstractListModel>

#include "media/MediaStreamTypes.h"
class SubtitleTrackModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        StreamIndexRole,
        SelectedRole,
        EnabledRole,
    };

    explicit SubtitleTrackModel(QObject* parent = nullptr);

    int rowCount(QModelIndex const& parent = {}) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTracks(std::vector<EmbeddedMediaStreamDescriptor> tracks, int selectedStreamIndex);
    void setSelectedStreamIndex(int streamIndex);
    int selectedStreamIndex() const;
    bool canSelect(int streamIndex) const;
    EmbeddedMediaStreamDescriptor const* track(int streamIndex) const;

  private:
    std::vector<EmbeddedMediaStreamDescriptor> m_tracks;
    int m_selectedStreamIndex = -1;
};
