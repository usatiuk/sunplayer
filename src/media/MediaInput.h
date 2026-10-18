#pragma once

#include <QFileInfo>
#include <QUrl>

// Internal source reference. A remote locator may contain credentials and must
// never be exposed through diagnostics, QML, or desktop media metadata.
struct MediaInput {
    QUrl locator;
    QString displayName;

    MediaInput() = default;
    MediaInput(QString const& path) : locator(QUrl::fromLocalFile(path)), displayName(QFileInfo(path).fileName()) {}
    MediaInput(QUrl url, QString label) : locator(std::move(url)), displayName(std::move(label)) {}

    bool isValid() const {
        return locator.isValid() && !locator.isEmpty() && !displayName.isEmpty() &&
               (locator.isLocalFile()
                    ? !locator.toLocalFile().isEmpty()
                    : ((locator.scheme() == QStringLiteral("http") || locator.scheme() == QStringLiteral("https")) &&
                       !locator.host().isEmpty() && locator.userInfo().isEmpty() && !locator.hasFragment()));
    }
    bool isRemote() const { return !locator.isLocalFile(); }
    QString ffmpegLocator() const {
        return locator.isLocalFile() ? locator.toLocalFile() : locator.toString(QUrl::FullyEncoded);
    }
    QUrl publicUrl() const { return isRemote() ? QUrl(QStringLiteral("media:remote")) : locator; }
};
