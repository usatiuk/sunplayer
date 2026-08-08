#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <QImage>
#include <QRect>

#include "subtitles/SubtitleTypes.h"

struct ass_library;
struct ass_renderer;
struct ass_track;
class QRhi;
class QRhiCommandBuffer;
class QRhiTexture;

class SubtitleRenderer final {
  public:
    explicit SubtitleRenderer(QRhi& rhi);
    ~SubtitleRenderer();

    SubtitleRenderer(SubtitleRenderer const&) = delete;
    SubtitleRenderer& operator=(SubtitleRenderer const&) = delete;

    bool prepare(SubtitlePresentationSnapshot const& snapshot, QRect const& videoRect, QSize const& targetSize,
                 bool active);
    void uploadIfNeeded(QRhiCommandBuffer& commandBuffer);

    QRhiTexture* texture() const;
    std::uint64_t textureRevision() const;
    QString error() const;

  private:
    struct AssBundle;

    bool ensureTexture(QSize const& size);
    bool ensureAssBundle(SubtitleStreamConfiguration const& configuration);
    bool processNewEvents(SubtitleStateSnapshot const& state);
    bool rasterize(SubtitlePresentationSnapshot const& snapshot, QRect const& videoRect, QSize const& targetSize,
                   bool forceRaster);
    void clearImage();
    void fail(QString error);
    void advanceTextureRevision();

    QRhi& m_rhi;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<AssBundle> m_ass;
    QImage m_image;
    QSize m_textureSize;
    std::uint64_t m_generation = 0;
    std::uint64_t m_failedGeneration = 0;
    std::uint64_t m_sourceRevision = 0;
    std::uint64_t m_textureRevision = 0;
    std::size_t m_processedEventCount = 0;
    std::shared_ptr<SubtitleBitmapComposition const> m_renderedBitmap;
    std::int64_t m_renderedTimeMicroseconds = -1;
    QRect m_renderedVideoRect;
    bool m_uploadPending = false;
    bool m_contentVisible = false;
    QString m_error;
};
