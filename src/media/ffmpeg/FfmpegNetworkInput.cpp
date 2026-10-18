#include "media/ffmpeg/FfmpegNetworkInput.h"

#include "diagnostics/LogCategories.h"
#include <QRegularExpression>
#include <mutex>
extern "C" {
#include <libavutil/log.h>
}

namespace {
// FFmpeg's default stderr logger includes URLs and HTTP queries. Redact at the
// library boundary, including messages emitted by its demux worker threads.
void safeFfmpegLog(void* context, int level, char const* format, va_list arguments) {
    if (level > AV_LOG_WARNING || level > av_log_get_level()) {
        return;
    }
    char buffer[2048]{};
    int prefix = 1;
    av_log_format_line2(context, level, format, arguments, buffer, sizeof(buffer), &prefix);
    QString message = QString::fromUtf8(buffer).trimmed();
    static QRegularExpression const url(QStringLiteral("https?://[^\\s\\\"']+"),
                                        QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression const secret(QStringLiteral("(api_key|token|authorization)[=:][^\\r\\n]*"),
                                           QRegularExpression::CaseInsensitiveOption);
    message.replace(url, QStringLiteral("<remote media>"));
    message.replace(secret, QStringLiteral("<redacted>"));
    if (!message.isEmpty()) {
        qCWarning(sunplayerLogMediaIo).noquote() << message;
    }
}
} // namespace

int openFfmpegInput(AVFormatContext** context, MediaInput const& input) {
    static std::once_flag logging;
    std::call_once(logging, [] { av_log_set_callback(safeFfmpegLog); });
    AVDictionary* options = nullptr;
    if (input.isRemote()) {
        av_dict_set(&options, "protocol_whitelist", "http,https,tcp,tls", 0);
        av_dict_set(&options, "rw_timeout", "15000000", 0);
        av_dict_set(&options, "tls_verify", "1", 0);
        // Authenticated URLs avoid reusable custom headers being forwarded by
        // native FFmpeg HTTP redirects to another origin.
    } else {
        av_dict_set(&options, "protocol_whitelist", "file", 0);
    }
    QByteArray const locator = input.ffmpegLocator().toUtf8();
    int const result = avformat_open_input(context, locator.constData(), nullptr, &options);
    av_dict_free(&options);
    return result;
}
