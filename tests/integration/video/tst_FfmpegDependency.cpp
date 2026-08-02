#include <QtTest>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#ifdef Q_OS_LINUX
#include <libavutil/hwcontext_drm.h>
#include <libavutil/hwcontext_vaapi.h>
#elif defined(Q_OS_MACOS)
#include <libavutil/hwcontext_videotoolbox.h>
#endif
#include <libswresample/swresample.h>
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class FfmpegDependencyTest final : public QObject {
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
    void pinnedMinimalFeatureSet();
};

void FfmpegDependencyTest::pinnedMinimalFeatureSet() {
    QCOMPARE(LIBAVFORMAT_VERSION_MAJOR, 62);
    QCOMPARE(LIBAVCODEC_VERSION_MAJOR, 62);
    QCOMPARE(LIBAVUTIL_VERSION_MAJOR, 60);
    QCOMPARE(LIBSWRESAMPLE_VERSION_MAJOR, 6);
    QCOMPARE(
        avformat_version(),
        static_cast<unsigned int>(LIBAVFORMAT_VERSION_INT));
    QCOMPARE(
        avcodec_version(),
        static_cast<unsigned int>(LIBAVCODEC_VERSION_INT));
    QCOMPARE(
        avutil_version(),
        static_cast<unsigned int>(LIBAVUTIL_VERSION_INT));
    QCOMPARE(
        swresample_version(),
        static_cast<unsigned int>(
            LIBSWRESAMPLE_VERSION_INT));

#ifdef Q_OS_WIN
    const QString configuration = QString::fromLatin1(
        avcodec_configuration());
    QVERIFY(configuration.contains(
        QStringLiteral("--enable-d3d11va")));
    QVERIFY(configuration.contains(
        QStringLiteral("--disable-vulkan")));
    QVERIFY(configuration.contains(
        QStringLiteral("--disable-swscale")));
    QCOMPARE(
        av_hwdevice_find_type_by_name("d3d11va"),
        AV_HWDEVICE_TYPE_D3D11VA);
#elif defined(Q_OS_LINUX)
    QVERIFY(sizeof(AVVAAPIDeviceContext) > 0);
    QVERIFY(sizeof(AVDRMFrameDescriptor) > 0);
    QCOMPARE(
        av_hwdevice_find_type_by_name("vaapi"),
        AV_HWDEVICE_TYPE_VAAPI);
    QCOMPARE(
        av_hwdevice_find_type_by_name("drm"),
        AV_HWDEVICE_TYPE_DRM);
#elif defined(Q_OS_MACOS)
    const QString configuration = QString::fromLatin1(
        avcodec_configuration());
    QVERIFY(sizeof(AVVTFramesContext) > 0);
    QVERIFY(configuration.contains(
        QStringLiteral("--enable-videotoolbox")));
    QVERIFY(configuration.contains(
        QStringLiteral("--disable-swscale")));
    QCOMPARE(
        av_hwdevice_find_type_by_name("videotoolbox"),
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX);
#else
#error "Define the required FFmpeg hardware facilities for this platform"
#endif
    QVERIFY(avcodec_find_decoder(AV_CODEC_ID_H264));
    QVERIFY(avcodec_find_decoder(AV_CODEC_ID_HEVC));
}

QTEST_APPLESS_MAIN(FfmpegDependencyTest)
#include "tst_FfmpegDependency.moc"
