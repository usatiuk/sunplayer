#include "app/LocalMediaDrop.h"

#include <QMimeData>

std::optional<QUrl> singleLocalMediaDropUrl(QMimeData const* mimeData, Qt::DropActions possibleActions) {
    if (!mimeData || !possibleActions.testFlag(Qt::CopyAction) || !mimeData->hasUrls()) {
        return std::nullopt;
    }

    QList<QUrl> const urls = mimeData->urls();
    if (urls.size() != 1) {
        return std::nullopt;
    }

    QUrl const& url = urls.constFirst();
    if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty()) {
        return std::nullopt;
    }
    return url;
}
