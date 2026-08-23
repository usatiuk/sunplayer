#include "app/SupportDiagnostics.h"

#include <cmath>

#include <QRegularExpression>
#include <QStringList>
#include <QUrlQuery>

namespace {
constexpr qsizetype detailedReportLimit = 8 * 1024;
constexpr qsizetype issueBodyLimit = 1800;

QString booleanValue(bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); }

QString mediaStateName(SupportMediaState state) {
    switch (state) {
    case SupportMediaState::Empty:
        return QStringLiteral("empty");
    case SupportMediaState::Opening:
        return QStringLiteral("opening");
    case SupportMediaState::Ready:
        return QStringLiteral("ready");
    case SupportMediaState::Error:
        return QStringLiteral("error");
    }
    Q_UNREACHABLE();
}

void appendMedia(QStringList& lines, QString const& prefix, SupportMediaSnapshot const& media) {
    lines.append(QStringLiteral("%1 state: %2").arg(prefix, mediaStateName(media.state)));
    lines.append(QStringLiteral("%1 video frame: %2").arg(prefix, booleanValue(media.hasVideoFrame)));
    lines.append(QStringLiteral("%1 dynamic range: %2")
                     .arg(prefix, media.videoHdr ? QStringLiteral("HDR") : QStringLiteral("SDR or unknown")));
    lines.append(QStringLiteral("%1 audio output: %2").arg(prefix, booleanValue(media.hasAudioOutput)));
    if (media.durationMilliseconds >= 0) {
        lines.append(QStringLiteral("%1 duration ms: %2").arg(prefix).arg(media.durationMilliseconds));
    }
    lines.append(QStringLiteral("%1 decoded/dropped video frames: %2/%3")
                     .arg(prefix)
                     .arg(media.decodedVideoFrames)
                     .arg(media.droppedVideoFrames));
    lines.append(QStringLiteral("%1 decoder: %2").arg(prefix, SupportDiagnostics::sanitizedToken(media.decoder)));
    lines.append(
        QStringLiteral("%1 decode path: %2").arg(prefix, SupportDiagnostics::sanitizedToken(media.decodePath)));
    lines.append(
        QStringLiteral("%1 audio backend: %2").arg(prefix, SupportDiagnostics::sanitizedToken(media.audioBackend)));
}

QString bounded(QString text, qsizetype maximumLength) {
    if (text.size() <= maximumLength) {
        return text;
    }
    static QString const suffix = QStringLiteral("\n[diagnostic summary truncated]\n");
    return text.left(maximumLength - suffix.size()) + suffix;
}
} // namespace

QString SupportDiagnostics::sanitizedToken(QString const& value, qsizetype maximumLength) {
    QString const trimmed = value.trimmed();
    if (trimmed.isEmpty() || maximumLength < 2) {
        return QStringLiteral("unavailable");
    }

    // Report fields are selected by the application rather than copied from
    // media metadata. Reject accidental path/URL/identity-shaped values and
    // controls, but preserve punctuation and Unicode used by real backend and
    // color-policy descriptions.
    for (QChar const character : trimmed) {
        if (!character.isPrint() && !character.isHighSurrogate() && !character.isLowSurrogate()) {
            return QStringLiteral("unavailable");
        }
    }
    static QRegularExpression const windowsPath(QStringLiteral("^[A-Za-z]:[\\\\/]|^\\\\\\\\"));
    if (windowsPath.match(trimmed).hasMatch() || trimmed.startsWith(u'/') || trimmed.startsWith(QStringLiteral("//")) ||
        trimmed.contains(QStringLiteral("://"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("user:"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("host:"), Qt::CaseInsensitive)) {
        return QStringLiteral("unavailable");
    }
    if (trimmed.size() <= maximumLength) {
        return trimmed;
    }
    return trimmed.left(maximumLength - 1) + u'…';
}

QString SupportDiagnostics::detailedReport(SupportSnapshot const& snapshot) {
    QStringList lines{
        QStringLiteral("SunPlayer diagnostic information"),
        QStringLiteral("Version: %1").arg(sanitizedToken(snapshot.version, 32)),
        QStringLiteral("Build ID: %1").arg(sanitizedToken(snapshot.buildId, 64)),
        QStringLiteral("Qt: %1").arg(sanitizedToken(snapshot.qtVersion, 32)),
        QStringLiteral("Operating system: %1").arg(sanitizedToken(snapshot.operatingSystem, 96)),
        QStringLiteral("CPU architecture: %1").arg(sanitizedToken(snapshot.cpuArchitecture, 32)),
        QStringLiteral("Debug logging enabled: %1").arg(booleanValue(snapshot.debugLoggingEnabled)),
        QStringLiteral("Graphics API: %1").arg(sanitizedToken(snapshot.graphicsApi)),
        QStringLiteral("Swap-chain format: %1").arg(sanitizedToken(snapshot.swapChainFormat)),
        QStringLiteral("Video surface producer: %1").arg(sanitizedToken(snapshot.videoSurfaceProducer)),
        QStringLiteral("Video color policy: %1").arg(sanitizedToken(snapshot.videoColorPolicy)),
        QStringLiteral("Display color mode: %1").arg(sanitizedToken(snapshot.displayColorMode)),
        QStringLiteral("Display HDR enabled: %1").arg(booleanValue(snapshot.displayHdrEnabled)),
        QStringLiteral("HDR presentation active: %1").arg(booleanValue(snapshot.hdrPresentationActive)),
    };
    if (snapshot.sdrWhiteKnown && std::isfinite(snapshot.sdrWhiteNits) && snapshot.sdrWhiteNits > 0.0f) {
        lines.append(QStringLiteral("SDR white nits: %1").arg(snapshot.sdrWhiteNits, 0, 'f', 1));
    } else {
        lines.append(QStringLiteral("SDR white nits: unavailable"));
    }
    if (std::isfinite(snapshot.effectiveTargetHeadroom) && snapshot.effectiveTargetHeadroom >= 1.0f) {
        lines.append(QStringLiteral("Effective HDR headroom: %1").arg(snapshot.effectiveTargetHeadroom, 0, 'f', 3));
    }
    if (!snapshot.applicationErrorCode.isEmpty()) {
        lines.append(QStringLiteral("Application error: %1 (%2)")
                         .arg(sanitizedToken(snapshot.applicationErrorCode, 64),
                              sanitizedToken(snapshot.applicationErrorSubsystem, 32)));
    }
    if (snapshot.currentMedia) {
        appendMedia(lines, QStringLiteral("Current media"), *snapshot.currentMedia);
    } else {
        lines.append(QStringLiteral("Current media: none"));
    }
    if (snapshot.lastMedia && !snapshot.currentMedia) {
        appendMedia(lines, QStringLiteral("Last media"), *snapshot.lastMedia);
    }
    lines.append(QString{});
    lines.append(
        QStringLiteral("Privacy: no raw logs, file names, paths, URLs, user/host names, or machine IDs are included."));
    return bounded(lines.join(u'\n') + u'\n', detailedReportLimit);
}

QString SupportDiagnostics::issueBody(SupportSnapshot const& snapshot) {
    QStringList lines{
        QStringLiteral("## What happened"),
        QStringLiteral("<!-- Describe the problem and how to reproduce it. -->"),
        {},
        QStringLiteral("## Environment"),
        QStringLiteral("- SunPlayer: %1 (%2)")
            .arg(sanitizedToken(snapshot.version, 32), sanitizedToken(snapshot.buildId, 64)),
        QStringLiteral("- OS: %1").arg(sanitizedToken(snapshot.operatingSystem, 96)),
        QStringLiteral("- Graphics API: %1").arg(sanitizedToken(snapshot.graphicsApi)),
        QStringLiteral("- HDR presentation: %1").arg(booleanValue(snapshot.hdrPresentationActive)),
    };
    if (!snapshot.applicationErrorCode.isEmpty()) {
        lines.append(QStringLiteral("- Error code: %1").arg(sanitizedToken(snapshot.applicationErrorCode, 64)));
    }
    if (snapshot.currentMedia) {
        lines.append(QStringLiteral("- Media state: %1; %2")
                         .arg(mediaStateName(snapshot.currentMedia->state), snapshot.currentMedia->videoHdr
                                                                                ? QStringLiteral("HDR")
                                                                                : QStringLiteral("SDR or unknown")));
    } else if (snapshot.lastMedia) {
        lines.append(QStringLiteral("- Last media state: %1; %2")
                         .arg(mediaStateName(snapshot.lastMedia->state),
                              snapshot.lastMedia->videoHdr ? QStringLiteral("HDR") : QStringLiteral("SDR or unknown")));
    }
    lines.append(QString{});
    lines.append(
        QStringLiteral("Detailed sanitized diagnostics were copied to the clipboard. Paste them here if useful."));
    return bounded(lines.join(u'\n') + u'\n', issueBodyLimit);
}

QUrl SupportDiagnostics::issueUrl(QUrl const& baseUrl, SupportSnapshot const& snapshot) {
    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("title"), QStringLiteral("Bug: "));
    query.addQueryItem(QStringLiteral("body"), issueBody(snapshot));
    url.setQuery(query);
    return url;
}
