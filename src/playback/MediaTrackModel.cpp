#include "playback/MediaTrackModel.h"

#include <utility>

MediaTrackModel::MediaTrackModel(QObject* parent) : QAbstractListModel(parent) {}

int MediaTrackModel::rowCount(QModelIndex const& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_tracks.size());
}

QVariant MediaTrackModel::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_tracks.size())) {
        return {};
    }
    EmbeddedMediaStreamDescriptor const& track = m_tracks[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return track.label;
    case StreamIndexRole:
        return track.streamIndex;
    case EnabledRole:
        return track.supported;
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaTrackModel::roleNames() const {
    return {
        {LabelRole, QByteArrayLiteral("label")},
        {StreamIndexRole, QByteArrayLiteral("streamIndex")},
        {EnabledRole, QByteArrayLiteral("available")},
    };
}

void MediaTrackModel::setTracks(std::vector<EmbeddedMediaStreamDescriptor> tracks) {
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

bool MediaTrackModel::canSelect(int streamIndex) const {
    EmbeddedMediaStreamDescriptor const* const selected = track(streamIndex);
    return selected && selected->supported;
}

EmbeddedMediaStreamDescriptor const* MediaTrackModel::track(int streamIndex) const {
    for (EmbeddedMediaStreamDescriptor const& candidate : m_tracks) {
        if (candidate.streamIndex == streamIndex) {
            return &candidate;
        }
    }
    return nullptr;
}

std::optional<std::int64_t> MediaTrackModel::endMicroseconds(int streamIndex) const {
    EmbeddedMediaStreamDescriptor const* const selected = track(streamIndex);
    return selected ? selected->endMicroseconds : std::nullopt;
}
