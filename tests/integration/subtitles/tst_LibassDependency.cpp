#include <QFile>
#include <QtTest>

extern "C" {
#include <ass/ass.h>
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class LibassDependencyTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void rendersEmbeddedFontAssCue();
};

void LibassDependencyTest::rendersEmbeddedFontAssCue() {
    QVERIFY(ass_library_version() > 0);
    ASS_Library* library = ass_library_init();
    QVERIFY(library);
    QFile font(QStringLiteral(SUNROOM_TEST_FIXTURE_DIR "/media/Ahem.ttf"));
    QVERIFY(font.open(QIODevice::ReadOnly));
    QByteArray const fontBytes = font.readAll();
    QVERIFY(!fontBytes.isEmpty());
    ass_add_font(library, "Ahem.ttf", fontBytes.constData(), static_cast<int>(fontBytes.size()));
    ASS_Renderer* renderer = ass_renderer_init(library);
    QVERIFY(renderer);
    ASS_Track* track = ass_new_track(library);
    QVERIFY(track);
    ass_set_frame_size(renderer, 320, 180);
    ass_set_fonts(renderer, nullptr, "Ahem", ASS_FONTPROVIDER_NONE, nullptr, 1);
    QByteArray header = QByteArrayLiteral("[Script Info]\n"
                                          "ScriptType: v4.00+\n"
                                          "PlayResX: 320\n"
                                          "PlayResY: 180\n"
                                          "[V4+ Styles]\n"
                                          "Format: Name, Fontname, Fontsize, PrimaryColour, "
                                          "SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
                                          "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                                          "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
                                          "MarginV, Encoding\n"
                                          "Style: Default,Ahem,28,&H00FFFFFF,&H000000FF,&H00000000,"
                                          "&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,10,10,10,1\n"
                                          "[Events]\n"
                                          "Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
                                          "MarginV, Effect, Text\n");
    ass_process_codec_private(track, header.data(), header.size());
    QByteArray cue = QByteArrayLiteral("0,0,Default,,0,0,0,,Subtitle");
    ass_process_chunk(track, cue.data(), cue.size(), 500, 2'000);
    int changed = 0;
    ASS_Image* image = ass_render_frame(renderer, track, 1'000, &changed);
    QVERIFY(image);
    QVERIFY(changed != 0);
    QVERIFY(image->w > 0);
    QVERIFY(image->h > 0);
    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(library);
}

QTEST_APPLESS_MAIN(LibassDependencyTest)
#include "tst_LibassDependency.moc"
