#include "presentation/SubtitleRenderer.h"

#include <algorithm>
#include <limits>

#include <QPainter>
#include <rhi/qrhi.h>

extern "C" {
#include <ass/ass.h>
}

namespace {
constexpr int maximumRasterDimension = 16'384;
constexpr qsizetype maximumRasterBytes = 256 * 1024 * 1024;

QByteArray const defaultAssHeader =
    QByteArrayLiteral("[Script Info]\n"
                      "ScriptType: v4.00+\n"
                      "PlayResX: 1920\n"
                      "PlayResY: 1080\n"
                      "ScaledBorderAndShadow: yes\n"
                      "[V4+ Styles]\n"
                      "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                      "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
                      "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                      "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                      "Style: Default,Arial,52,&H00FFFFFF,&H000000FF,&H00101010,"
                      "&H80000000,0,0,0,0,100,100,0,0,1,3,1,2,40,40,42,1\n"
                      "[Events]\n"
                      "Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
                      "MarginV, Effect, Text\n");

void blendAssImage(QImage& target, ASS_Image const& image, QPoint offset) {
    if (!image.bitmap || image.w <= 0 || image.h <= 0 || image.stride < image.w) {
        return;
    }
    QImage layer(image.w, image.h, QImage::Format_RGBA8888_Premultiplied);
    if (layer.isNull()) {
        return;
    }
    unsigned char const red = static_cast<unsigned char>((image.color >> 24U) & 0xffU);
    unsigned char const green = static_cast<unsigned char>((image.color >> 16U) & 0xffU);
    unsigned char const blue = static_cast<unsigned char>((image.color >> 8U) & 0xffU);
    unsigned int const opacity = 255U - (image.color & 0xffU);
    for (int y = 0; y < image.h; ++y) {
        auto* output = layer.scanLine(y);
        unsigned char const* coverage = image.bitmap + static_cast<std::ptrdiff_t>(y) * image.stride;
        for (int x = 0; x < image.w; ++x) {
            unsigned int const alpha = static_cast<unsigned int>(coverage[x]) * opacity / 255U;
            *output++ = static_cast<unsigned char>(red * alpha / 255U);
            *output++ = static_cast<unsigned char>(green * alpha / 255U);
            *output++ = static_cast<unsigned char>(blue * alpha / 255U);
            *output++ = static_cast<unsigned char>(alpha);
        }
    }
    QPainter painter(&target);
    painter.drawImage(offset + QPoint(image.dst_x, image.dst_y), layer);
}

void drawBitmapComposition(QImage& target, SubtitleBitmapComposition const& composition, QRect const& videoRect) {
    if (!composition.isValid() || videoRect.isEmpty()) {
        return;
    }
    qreal const scaleX = static_cast<qreal>(videoRect.width()) / composition.canvasSize.width();
    qreal const scaleY = static_cast<qreal>(videoRect.height()) / composition.canvasSize.height();
    QPainter painter(&target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (SubtitleBitmapRegion const& region : composition.regions) {
        QImage source(reinterpret_cast<uchar const*>(region.rgba.constData()), region.size.width(),
                      region.size.height(), region.size.width() * 4, QImage::Format_RGBA8888);
        QRectF const destination(videoRect.x() + region.x * scaleX, videoRect.y() + region.y * scaleY,
                                 region.size.width() * scaleX, region.size.height() * scaleY);
        painter.drawImage(destination, source);
    }
}
} // namespace

struct SubtitleRenderer::AssBundle {
    ASS_Library* library = nullptr;
    ASS_Renderer* renderer = nullptr;
    ASS_Track* track = nullptr;

    ~AssBundle() {
        if (track) {
            ass_free_track(track);
        }
        if (renderer) {
            ass_renderer_done(renderer);
        }
        if (library) {
            ass_library_done(library);
        }
    }
};

SubtitleRenderer::SubtitleRenderer(QRhi& rhi) : m_rhi(rhi) {
    ensureTexture({1, 1});
    clearImage();
}

SubtitleRenderer::~SubtitleRenderer() = default;

bool SubtitleRenderer::prepare(SubtitlePresentationSnapshot const& snapshot, QRect const& videoRect,
                               QSize const& targetSize, bool active) {
    if (!active || !snapshot.state.isEnabled() || targetSize.isEmpty() || videoRect.isEmpty()) {
        bool const wasVisible = m_contentVisible;
        m_contentVisible = false;
        m_generation = snapshot.state.playbackGeneration;
        m_ass.reset();
        m_processedEventCount = 0;
        m_renderedBitmap.reset();
        if (wasVisible || m_textureSize != QSize(1, 1)) {
            ensureTexture({1, 1});
            clearImage();
        }
        return true;
    }

    if (m_generation != snapshot.state.playbackGeneration) {
        m_generation = snapshot.state.playbackGeneration;
        m_failedGeneration = 0;
        m_sourceRevision = 0;
        m_processedEventCount = 0;
        m_renderedBitmap.reset();
        m_renderedTimeMicroseconds = -1;
        m_ass.reset();
        m_error.clear();
    }
    if (m_failedGeneration == m_generation) {
        return false;
    }
    if (targetSize.width() > maximumRasterDimension || targetSize.height() > maximumRasterDimension ||
        static_cast<qint64>(targetSize.width()) * targetSize.height() * 4 > maximumRasterBytes) {
        fail(QStringLiteral("Subtitle raster exceeds its size budget"));
        return false;
    }
    bool const forceRaster = m_textureSize != targetSize || m_renderedVideoRect != videoRect;
    if (!processNewEvents(snapshot.state) || !ensureTexture(targetSize)) {
        return false;
    }
    m_error.clear();

    bool const requiresRaster = forceRaster || m_sourceRevision != snapshot.state.revision ||
                                m_renderedTimeMicroseconds != snapshot.mediaTimeMicroseconds ||
                                m_renderedVideoRect != videoRect || m_image.size() != targetSize;
    if (!requiresRaster) {
        return true;
    }
    if (!rasterize(snapshot, videoRect, targetSize, forceRaster)) {
        return false;
    }
    m_sourceRevision = snapshot.state.revision;
    m_renderedTimeMicroseconds = snapshot.mediaTimeMicroseconds;
    m_renderedVideoRect = videoRect;
    return true;
}

void SubtitleRenderer::uploadIfNeeded(QRhiCommandBuffer& commandBuffer) {
    if (!m_uploadPending || !m_texture || m_image.isNull()) {
        return;
    }
    QRhiResourceUpdateBatch* updates = m_rhi.nextResourceUpdateBatch();
    updates->uploadTexture(m_texture.get(), m_image);
    commandBuffer.resourceUpdate(updates);
    m_uploadPending = false;
}

QRhiTexture* SubtitleRenderer::texture() const { return m_texture.get(); }

std::uint64_t SubtitleRenderer::textureRevision() const { return m_textureRevision; }

QString SubtitleRenderer::error() const { return m_error; }

bool SubtitleRenderer::ensureTexture(QSize const& size) {
    if (m_texture && m_textureSize == size) {
        return true;
    }
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull()) {
        fail(QStringLiteral("Could not allocate the subtitle raster"));
        return false;
    }
    std::unique_ptr<QRhiTexture> texture(m_rhi.newTexture(QRhiTexture::RGBA8, size, 1));
    if (!texture) {
        fail(QStringLiteral("Could not allocate the subtitle texture"));
        return false;
    }
    texture->setName(QByteArrayLiteral("SunPlayer subtitle layer"));
    if (!texture->create()) {
        fail(QStringLiteral("Could not create the subtitle texture"));
        return false;
    }
    m_texture = std::move(texture);
    m_textureSize = size;
    m_image = std::move(image);
    m_image.fill(Qt::transparent);
    advanceTextureRevision();
    m_uploadPending = true;
    return true;
}

bool SubtitleRenderer::ensureAssBundle(SubtitleStreamConfiguration const& configuration) {
    if (m_ass) {
        return true;
    }
    auto bundle = std::make_unique<AssBundle>();
    bundle->library = ass_library_init();
    if (!bundle->library) {
        fail(QStringLiteral("Could not initialize libass"));
        return false;
    }
    ass_set_extract_fonts(bundle->library, 1);
    for (SubtitleFontAttachment const& font : configuration.fonts) {
        ass_add_font(bundle->library, font.name.toUtf8().constData(), font.bytes.constData(), font.bytes.size());
    }
    bundle->renderer = ass_renderer_init(bundle->library);
    bundle->track = ass_new_track(bundle->library);
    if (!bundle->renderer || !bundle->track) {
        fail(QStringLiteral("Could not create the libass renderer"));
        return false;
    }
    ass_set_fonts(bundle->renderer, nullptr, "Arial", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
    QByteArray const& header = configuration.codecPrivate.isEmpty() ? defaultAssHeader : configuration.codecPrivate;
    ass_process_codec_private(bundle->track, const_cast<char*>(header.constData()), header.size());
    m_ass = std::move(bundle);
    return true;
}

bool SubtitleRenderer::processNewEvents(SubtitleStateSnapshot const& state) {
    if (!state.events) {
        return false;
    }
    if (m_processedEventCount > state.events->size()) {
        fail(QStringLiteral("Subtitle event history was replaced unexpectedly"));
        return false;
    }
    for (; m_processedEventCount < state.events->size(); ++m_processedEventCount) {
        SubtitleEvent const& event = (*state.events)[m_processedEventCount];
        if (event.type != SubtitlePayloadType::AssText) {
            continue;
        }
        if (!m_ass && (!state.configuration || !ensureAssBundle(*state.configuration))) {
            return false;
        }
        std::int64_t const start = event.startMicroseconds / 1'000;
        std::int64_t const duration =
            event.endMicroseconds
                ? std::max<std::int64_t>(1, (*event.endMicroseconds - event.startMicroseconds) / 1'000)
                : 24LL * 60LL * 60LL * 1'000LL;
        ass_process_chunk(m_ass->track, const_cast<char*>(event.ass.constData()), event.ass.size(), start, duration);
    }
    return true;
}

bool SubtitleRenderer::rasterize(SubtitlePresentationSnapshot const& snapshot, QRect const& videoRect,
                                 QSize const& targetSize, bool forceRaster) {
    if (m_image.size() != targetSize) {
        m_image = QImage(targetSize, QImage::Format_RGBA8888_Premultiplied);
        if (m_image.isNull()) {
            fail(QStringLiteral("Could not allocate the subtitle raster"));
            return false;
        }
    }
    std::shared_ptr<SubtitleBitmapComposition const> bitmap;
    for (SubtitleEvent const& event : *snapshot.state.events) {
        if (event.startMicroseconds > snapshot.mediaTimeMicroseconds) {
            break;
        }
        if (event.type == SubtitlePayloadType::Clear) {
            bitmap.reset();
        } else if (event.type == SubtitlePayloadType::Bitmap) {
            bitmap = event.endMicroseconds && snapshot.mediaTimeMicroseconds >= *event.endMicroseconds ? nullptr
                                                                                                       : event.bitmap;
        }
    }
    ASS_Image* assImage = nullptr;
    int assChanged = 0;
    if (m_ass) {
        ass_set_frame_size(m_ass->renderer, videoRect.width(), videoRect.height());
        QSize const storage = snapshot.state.configuration->canvasSize.isEmpty()
                                  ? videoRect.size()
                                  : snapshot.state.configuration->canvasSize;
        ass_set_storage_size(m_ass->renderer, storage.width(), storage.height());
        assImage = ass_render_frame(m_ass->renderer, m_ass->track, snapshot.mediaTimeMicroseconds / 1'000, &assChanged);
    }

    if (!forceRaster && bitmap == m_renderedBitmap && assChanged == 0) {
        return true;
    }

    m_image.fill(Qt::transparent);
    if (bitmap) {
        drawBitmapComposition(m_image, *bitmap, videoRect);
    }
    for (ASS_Image* current = assImage; current; current = current->next) {
        blendAssImage(m_image, *current, videoRect.topLeft());
    }

    m_renderedBitmap = std::move(bitmap);
    m_contentVisible = m_renderedBitmap != nullptr || assImage != nullptr;
    m_uploadPending = true;
    return true;
}

void SubtitleRenderer::clearImage() {
    if (m_image.isNull()) {
        QSize const size = m_texture && !m_textureSize.isEmpty() ? m_textureSize : QSize(1, 1);
        m_image = QImage(size, QImage::Format_RGBA8888_Premultiplied);
    }
    if (!m_image.isNull()) {
        m_image.fill(Qt::transparent);
    }
    m_uploadPending = m_texture && !m_image.isNull();
}

void SubtitleRenderer::fail(QString error) {
    m_error = std::move(error);
    m_failedGeneration = m_generation;
    m_ass.reset();
    m_sourceRevision = 0;
    m_processedEventCount = 0;
    m_renderedBitmap.reset();
    m_renderedTimeMicroseconds = -1;
    m_renderedVideoRect = {};
    m_contentVisible = false;
    clearImage();
}

void SubtitleRenderer::advanceTextureRevision() {
    ++m_textureRevision;
    if (m_textureRevision == 0) {
        ++m_textureRevision;
    }
}
