#pragma once

#include <optional>

#include <QUrl>
#include <QtCore/qnamespace.h>

class QMimeData;

// Returns the one local URL a drop may open. This deliberately does not touch
// the filesystem: MediaSession owns open failures, including remote UNC I/O.
std::optional<QUrl> singleLocalMediaDropUrl(QMimeData const* mimeData, Qt::DropActions possibleActions);
