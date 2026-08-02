#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#include <QFile>
#include <QGuiApplication>
#include <QtTest>
#include <rhi/qrhi.h>

#include "graphics/GraphicsBackendFactory.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "presentation/SubtitleRenderer.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
struct BytePixel {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

bool readTexture(
        QRhi &rhi,
        SubtitleRenderer &renderer,
        QRhiReadbackResult &result) {
    if (!renderer.texture())
        return false;
    QRhiCommandBuffer *commandBuffer = nullptr;
    if (rhi.beginOffscreenFrame(&commandBuffer)
            != QRhi::FrameOpSuccess) {
        return false;
    }
    renderer.uploadIfNeeded(*commandBuffer);
    bool complete = false;
    result.completed = [&complete] { complete = true; };
    QRhiResourceUpdateBatch *updates = rhi.nextResourceUpdateBatch();
    updates->readBackTexture(
        QRhiReadbackDescription(renderer.texture()), &result);
    commandBuffer->resourceUpdate(updates);
    return rhi.endOffscreenFrame() == QRhi::FrameOpSuccess && complete;
}

BytePixel pixel(
        const QRhiReadbackResult &result,
        const QRhi &rhi,
        int x,
        int y) {
    const int row = rhi.isYUpInFramebuffer()
        ? result.pixelSize.height() - 1 - y
        : y;
    const qsizetype offset =
        (row * result.pixelSize.width() + x) * 4;
    const auto *value = reinterpret_cast<const uchar *>(
        result.data.constData() + offset);
    return {value[0], value[1], value[2], value[3]};
}

std::size_t visiblePixelCount(const QRhiReadbackResult &result) {
    std::size_t count = 0;
    const auto *bytes = reinterpret_cast<const uchar *>(result.data.constData());
    for (qsizetype offset = 3; offset < result.data.size(); offset += 4) {
        if (bytes[offset] != 0)
            ++count;
    }
    return count;
}

QRect visibleBounds(
        const QRhiReadbackResult &result,
        const QRhi &rhi) {
    QRect bounds;
    for (int y = 0; y < result.pixelSize.height(); ++y) {
        for (int x = 0; x < result.pixelSize.width(); ++x) {
            if (pixel(result, rhi, x, y).a != 0)
                bounds |= QRect(x, y, 1, 1);
        }
    }
    return bounds;
}

std::shared_ptr<const SubtitleBitmapComposition> bitmap(
        QPoint origin,
        QSize size,
        QByteArray rgba) {
    return std::make_shared<const SubtitleBitmapComposition>(
        SubtitleBitmapComposition{
            .canvasSize = {32, 18},
            .regions = {{
                .x = origin.x(),
                .y = origin.y(),
                .size = size,
                .rgba = std::move(rgba),
            }},
        });
}

QByteArray solidRgba(QSize size, uchar red, uchar green, uchar blue) {
    QByteArray bytes(size.width() * size.height() * 4, Qt::Uninitialized);
    auto *output = reinterpret_cast<uchar *>(bytes.data());
    for (int pixelIndex = 0;
            pixelIndex < size.width() * size.height();
            ++pixelIndex) {
        *output++ = red;
        *output++ = green;
        *output++ = blue;
        *output++ = 255;
    }
    return bytes;
}
}

class SubtitleRendererTest final : public QObject {
    Q_OBJECT

public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(
            SEM_FAILCRITICALERRORS
            | SEM_NOGPFAULTERRORBOX
            | SEM_NOOPENFILEERRORBOX);
#endif
    }

private slots:
    void rendersAssAndRestoresThePausedRoute();
    void replacesAndClearsBitmapCompositions();
    void failureIsLatchedUntilNextGeneration();
};

void SubtitleRendererTest::rendersAssAndRestoresThePausedRoute() {
#ifndef Q_OS_WIN
    QSKIP("The supported QRhi subtitle integration test is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY(graphicsDevice);
    QRhi &rhi = graphicsDevice->rhi();
    SubtitleRenderer renderer(rhi);

    QFile font(QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/Ahem.ttf"));
    QVERIFY(font.open(QIODevice::ReadOnly));
    const QByteArray header = QByteArrayLiteral(
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "PlayResX: 320\n"
        "PlayResY: 180\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
        "Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Ahem,30,&H00FFFFFF,&H000000FF,&H00000000,"
        "&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,10,10,20,1\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
        "MarginV, Effect, Text\n");
    auto events = std::make_shared<const std::vector<SubtitleEvent>>(
        std::vector<SubtitleEvent>{{
            .playbackGeneration = 1,
            .startMicroseconds = 0,
            .endMicroseconds = 5'000'000,
            .type = SubtitlePayloadType::AssText,
            .ass = QByteArrayLiteral(
                "0,0,Default,,0,0,0,,{\\t(0,5000,\\fscx200)}ABCD"),
        }});
    SubtitlePresentationSnapshot snapshot{
        .state = {
            .playbackGeneration = 1,
            .revision = 1,
            .configuration =
                std::make_shared<const SubtitleStreamConfiguration>(
                    SubtitleStreamConfiguration{
                        .playbackGeneration = 1,
                        .streamIndex = 2,
                        .codec = QStringLiteral("ass"),
                        .codecPrivate = header,
                        .canvasSize = {320, 180},
                        .fonts = {{
                            .name = QStringLiteral("Ahem.ttf"),
                            .bytes = font.readAll(),
                        }},
                    }),
            .events = std::move(events),
        },
        .mediaTimeMicroseconds = 1'000'000,
    };
    const QSize targetSize{320, 180};
    const QRect videoRect(QPoint{}, targetSize);

    QVERIFY(renderer.prepare(snapshot, videoRect, targetSize, true));
    QRhiReadbackResult visible;
    QVERIFY(readTexture(rhi, renderer, visible));
    QCOMPARE(visible.pixelSize, targetSize);
    QVERIFY(visiblePixelCount(visible) > 100);
    const QRect assBounds = visibleBounds(visible, rhi);
    QVERIFY(!assBounds.isEmpty());
    QVERIFY(assBounds.top() > targetSize.height() / 2);
    QVERIFY(assBounds.width() > assBounds.height() * 2);
    QVERIFY(std::abs(assBounds.center().x() - targetSize.width() / 2) < 30);

    snapshot.mediaTimeMicroseconds = 4'000'000;
    QVERIFY(renderer.prepare(snapshot, videoRect, targetSize, true));
    QRhiReadbackResult animated;
    QVERIFY(readTexture(rhi, renderer, animated));
    const QRect animatedBounds = visibleBounds(animated, rhi);
    QVERIFY(animatedBounds.width() > assBounds.width());

    QVERIFY(renderer.prepare(snapshot, videoRect, targetSize, false));
    QRhiReadbackResult hidden;
    QVERIFY(readTexture(rhi, renderer, hidden));
    QCOMPARE(hidden.pixelSize, QSize(1, 1));
    QCOMPARE(visiblePixelCount(hidden), 0U);

    // Returning to Player at the same paused media time must rerasterize.
    QVERIFY(renderer.prepare(snapshot, videoRect, targetSize, true));
    QRhiReadbackResult restored;
    QVERIFY(readTexture(rhi, renderer, restored));
    QCOMPARE(restored.pixelSize, targetSize);
    QVERIFY(visiblePixelCount(restored) > 100);
    QVERIFY2(renderer.error().isEmpty(), qPrintable(renderer.error()));
#endif
}

void SubtitleRendererTest::replacesAndClearsBitmapCompositions() {
#ifndef Q_OS_WIN
    QSKIP("The supported QRhi subtitle integration test is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY(graphicsDevice);
    QRhi &rhi = graphicsDevice->rhi();
    SubtitleRenderer renderer(rhi);
    const QSize targetSize{64, 40};
    const QRect videoRect(8, 4, 48, 27);
    auto events = std::make_shared<const std::vector<SubtitleEvent>>(
        std::vector<SubtitleEvent>{
            {
                .playbackGeneration = 2,
                .startMicroseconds = 0,
                .type = SubtitlePayloadType::Bitmap,
                .bitmap = bitmap(
                    {2, 3}, {4, 2},
                    solidRgba({4, 2}, 255, 0, 0)),
            },
            {
                .playbackGeneration = 2,
                .startMicroseconds = 1'000'000,
                .type = SubtitlePayloadType::Bitmap,
                .bitmap = bitmap(
                    {20, 10}, {3, 3},
                    solidRgba({3, 3}, 0, 0, 255)),
            },
            {
                .playbackGeneration = 2,
                .startMicroseconds = 2'000'000,
                .type = SubtitlePayloadType::Clear,
            },
        });
    SubtitlePresentationSnapshot snapshot{
        .state = {
            .playbackGeneration = 2,
            .revision = 1,
            .configuration =
                std::make_shared<const SubtitleStreamConfiguration>(
                    SubtitleStreamConfiguration{
                        .playbackGeneration = 2,
                        .streamIndex = 3,
                        .codec = QStringLiteral("hdmv_pgs_subtitle"),
                        .canvasSize = {32, 18},
                    }),
            .events = std::move(events),
        },
    };

    snapshot.mediaTimeMicroseconds = 500'000;
    QVERIFY(renderer.prepare(snapshot, videoRect, targetSize, true));
    QRhiReadbackResult first;
    QVERIFY(readTexture(rhi, renderer, first));
    QCOMPARE(pixel(first, rhi, 2, 3).a, 0);
    const BytePixel firstPixel = pixel(first, rhi, 13, 9);
    QVERIFY(firstPixel.r > 240 && firstPixel.g < 10
            && firstPixel.b < 10 && firstPixel.a == 255);

    snapshot.mediaTimeMicroseconds = 1'500'000;
    QVERIFY(renderer.prepare(snapshot, videoRect, targetSize, true));
    QRhiReadbackResult second;
    QVERIFY(readTexture(rhi, renderer, second));
    QCOMPARE(pixel(second, rhi, 13, 9).a, 0);
    const BytePixel secondPixel = pixel(second, rhi, 40, 21);
    QVERIFY(secondPixel.r < 10 && secondPixel.g < 10
            && secondPixel.b > 240 && secondPixel.a == 255);

    snapshot.mediaTimeMicroseconds = 2'500'000;
    QVERIFY(renderer.prepare(snapshot, videoRect, targetSize, true));
    QRhiReadbackResult cleared;
    QVERIFY(readTexture(rhi, renderer, cleared));
    QCOMPARE(visiblePixelCount(cleared), 0U);
#endif
}

void SubtitleRendererTest::failureIsLatchedUntilNextGeneration() {
#ifndef Q_OS_WIN
    QSKIP("The supported QRhi subtitle integration test is Windows D3D11");
#else
    std::unique_ptr<GraphicsDeviceDomain> graphicsDevice =
        GraphicsBackendFactory::createDeviceDomain();
    QVERIFY(graphicsDevice);
    QRhi &rhi = graphicsDevice->rhi();
    SubtitleRenderer renderer(rhi);

    const auto snapshotFor = [](std::uint64_t generation) {
        return SubtitlePresentationSnapshot{
            .state = {
                .playbackGeneration = generation,
                .revision = 1,
                .configuration = std::make_shared<
                    const SubtitleStreamConfiguration>(
                        SubtitleStreamConfiguration{
                            .playbackGeneration = generation,
                            .streamIndex = 2,
                            .codec = QStringLiteral(
                                "hdmv_pgs_subtitle"),
                            .canvasSize = {32, 18},
                        }),
                .events = std::make_shared<
                    const std::vector<SubtitleEvent>>(
                        std::vector<SubtitleEvent>{{
                            .playbackGeneration = generation,
                            .startMicroseconds = 0,
                            .type = SubtitlePayloadType::Bitmap,
                            .bitmap = bitmap(
                                {2, 3}, {4, 2},
                                solidRgba(
                                    {4, 2}, 255, 0, 0)),
                        }}),
            },
        };
    };

    SubtitlePresentationSnapshot failed = snapshotFor(3);
    QVERIFY(renderer.prepare(
        failed,
        QRect(QPoint{}, QSize(32, 18)),
        QSize(32, 18),
        true));
    QRhiReadbackResult visible;
    QVERIFY(readTexture(rhi, renderer, visible));
    QVERIFY(visiblePixelCount(visible) > 0);

    QVERIFY(!renderer.prepare(
        failed,
        QRect(0, 0, 1, 1),
        QSize(16'385, 1),
        true));
    QVERIFY(!renderer.error().isEmpty());
    QRhiReadbackResult transparent;
    QVERIFY(readTexture(rhi, renderer, transparent));
    QCOMPARE(visiblePixelCount(transparent), 0U);
    QVERIFY(!renderer.prepare(
        failed,
        QRect(QPoint{}, QSize(32, 18)),
        QSize(32, 18),
        true));

    SubtitlePresentationSnapshot recovered = snapshotFor(4);
    QVERIFY(renderer.prepare(
        recovered,
        QRect(QPoint{}, QSize(32, 18)),
        QSize(32, 18),
        true));
    QVERIFY(renderer.error().isEmpty());
#endif
}

int main(int argc, char **argv) {
    SubtitleRendererTest::initMain();
    QGuiApplication application(argc, argv);
    SubtitleRendererTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_SubtitleRenderer.moc"
