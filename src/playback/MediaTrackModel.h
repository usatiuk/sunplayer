#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <QAbstractListModel>

#include "media/MediaStreamTypes.h"

class MediaTrackModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        StreamIndexRole,
        EnabledRole,
    };

    explicit MediaTrackModel(QObject* parent = nullptr);

    int rowCount(QModelIndex const& parent = {}) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTracks(std::vector<EmbeddedMediaStreamDescriptor> tracks);
    bool canSelect(int streamIndex) const;
    EmbeddedMediaStreamDescriptor const* track(int streamIndex) const;
    std::optional<std::int64_t> endMicroseconds(int streamIndex) const;

  private:
    std::vector<EmbeddedMediaStreamDescriptor> m_tracks;
};
