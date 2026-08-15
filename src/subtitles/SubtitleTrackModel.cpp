#include "subtitles/SubtitleTrackModel.h"

#include <algorithm>
#include <utility>

SubtitleTrackModel::SubtitleTrackModel(QObject* parent) : QAbstractListModel(parent) {}

int SubtitleTrackModel::rowCount(QModelIndex const& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_tracks.size()) + 1;
}

QVariant SubtitleTrackModel::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    bool const off = index.row() == 0;
    EmbeddedMediaStreamDescriptor const* const track =
        off ? nullptr : &m_tracks[static_cast<std::size_t>(index.row() - 1)];
    int const streamIndex = track ? track->streamIndex : -1;
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return track ? track->label : QStringLiteral("Off");
    case StreamIndexRole:
        return streamIndex;
    case SelectedRole:
        return streamIndex == m_selectedStreamIndex;
    case EnabledRole:
        return !track || track->supported;
    default:
        return {};
    }
}

QHash<int, QByteArray> SubtitleTrackModel::roleNames() const {
    return {
        {LabelRole, QByteArrayLiteral("label")},
        {StreamIndexRole, QByteArrayLiteral("streamIndex")},
        {SelectedRole, QByteArrayLiteral("selected")},
        {EnabledRole, QByteArrayLiteral("available")},
    };
}

void SubtitleTrackModel::setTracks(std::vector<EmbeddedMediaStreamDescriptor> tracks, int selectedStreamIndex) {
    beginResetModel();
    std::erase_if(tracks, [](EmbeddedMediaStreamDescriptor const& track) { return !track.isValid(); });
    m_tracks = std::move(tracks);
    m_selectedStreamIndex = selectedStreamIndex;
    endResetModel();
}

void SubtitleTrackModel::setSelectedStreamIndex(int streamIndex) {
    if (m_selectedStreamIndex == streamIndex) {
        return;
    }
    int const previous = m_selectedStreamIndex;
    m_selectedStreamIndex = streamIndex;
    for (int row = 0; row < rowCount(); ++row) {
        int const candidate = row == 0 ? -1 : m_tracks[static_cast<std::size_t>(row - 1)].streamIndex;
        if (candidate == previous || candidate == streamIndex) {
            QModelIndex const changed = index(row, 0);
            emit dataChanged(changed, changed, {SelectedRole});
        }
    }
}

int SubtitleTrackModel::selectedStreamIndex() const { return m_selectedStreamIndex; }

bool SubtitleTrackModel::canSelect(int streamIndex) const {
    if (streamIndex == -1) {
        return true;
    }
    EmbeddedMediaStreamDescriptor const* const selected = track(streamIndex);
    return selected && selected->supported;
}

EmbeddedMediaStreamDescriptor const* SubtitleTrackModel::track(int streamIndex) const {
    for (EmbeddedMediaStreamDescriptor const& candidate : m_tracks) {
        if (candidate.streamIndex == streamIndex) {
            return &candidate;
        }
    }
    return nullptr;
}
