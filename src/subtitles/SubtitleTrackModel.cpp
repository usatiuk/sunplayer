#include "subtitles/SubtitleTrackModel.h"

#include <utility>

SubtitleTrackModel::SubtitleTrackModel(QObject* parent) : QAbstractListModel(parent) {
    m_entries.push_back({
        .label = QStringLiteral("Off"),
        .streamIndex = -1,
    });
}

int SubtitleTrackModel::rowCount(QModelIndex const& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant SubtitleTrackModel::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }
    Entry const& entry = m_entries[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return entry.label;
    case StreamIndexRole:
        return entry.streamIndex;
    case SelectedRole:
        return entry.streamIndex == m_selectedStreamIndex;
    case EnabledRole:
        return entry.enabled;
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
    m_entries.clear();
    m_entries.push_back({
        .label = QStringLiteral("Off"),
        .streamIndex = -1,
    });
    for (EmbeddedMediaStreamDescriptor& track : tracks) {
        if (!track.isValid()) {
            continue;
        }
        m_entries.push_back({
            .label = std::move(track.label),
            .streamIndex = track.streamIndex,
            .enabled = track.supported,
        });
    }
    m_selectedStreamIndex = selectedStreamIndex;
    endResetModel();
}

void SubtitleTrackModel::setSelectedStreamIndex(int streamIndex) {
    if (m_selectedStreamIndex == streamIndex) {
        return;
    }
    int const previous = m_selectedStreamIndex;
    m_selectedStreamIndex = streamIndex;
    for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
        int const candidate = m_entries[static_cast<std::size_t>(row)].streamIndex;
        if (candidate == previous || candidate == streamIndex) {
            QModelIndex const changed = index(row, 0);
            emit dataChanged(changed, changed, {SelectedRole});
        }
    }
}

int SubtitleTrackModel::selectedStreamIndex() const { return m_selectedStreamIndex; }

bool SubtitleTrackModel::canSelect(int streamIndex) const {
    for (Entry const& entry : m_entries) {
        if (entry.streamIndex == streamIndex) {
            return entry.enabled;
        }
    }
    return false;
}
