#include <QtTest>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
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
    QCOMPARE(
        avformat_version(),
        static_cast<unsigned int>(LIBAVFORMAT_VERSION_INT));
    QCOMPARE(
        avcodec_version(),
        static_cast<unsigned int>(LIBAVCODEC_VERSION_INT));

    const QString configuration =
        QString::fromLatin1(avcodec_configuration());
    QVERIFY(configuration.contains(
        QStringLiteral("--enable-d3d11va")));
    QVERIFY(configuration.contains(
        QStringLiteral("--disable-vulkan")));
    QVERIFY(configuration.contains(
        QStringLiteral("--disable-swscale")));
    QCOMPARE(
        av_hwdevice_find_type_by_name("d3d11va"),
        AV_HWDEVICE_TYPE_D3D11VA);
    QVERIFY(avcodec_find_decoder(AV_CODEC_ID_H264));
    QVERIFY(avcodec_find_decoder(AV_CODEC_ID_HEVC));
}

QTEST_APPLESS_MAIN(FfmpegDependencyTest)
#include "tst_FfmpegDependency.moc"
