#include <QtTest>

#include <libplacebo/config.h>
#include <libplacebo/log.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifdef Q_OS_WIN

#if !defined(PL_HAVE_D3D11) || !PL_HAVE_D3D11
#error "The initial Windows libplacebo build must include D3D11"
#endif

#if !defined(PL_HAVE_SHADERC) || !PL_HAVE_SHADERC
#error "The initial Windows libplacebo build must include Shaderc"
#endif

#if !defined(PL_HAVE_DOVI) || !PL_HAVE_DOVI
#error "The initial Windows libplacebo build must include built-in DOVI support"
#endif

#if defined(PL_HAVE_LIBDOVI) && PL_HAVE_LIBDOVI
#error "The initial Windows libplacebo build must not depend on libdovi"
#endif

#if defined(PL_HAVE_VULKAN) && PL_HAVE_VULKAN
#error "The initial Windows libplacebo build must not include Vulkan"
#endif

#if defined(PL_HAVE_OPENGL) && PL_HAVE_OPENGL
#error "The initial Windows libplacebo build must not include OpenGL"
#endif

#elif defined(Q_OS_LINUX)

#if !defined(PL_HAVE_VULKAN) || !PL_HAVE_VULKAN
#error "The initial Linux libplacebo build must include Vulkan"
#endif

#if (!defined(PL_HAVE_GLSLANG) || !PL_HAVE_GLSLANG) \
        && (!defined(PL_HAVE_SHADERC) || !PL_HAVE_SHADERC)
#error "The initial Linux libplacebo build needs glslang or Shaderc"
#endif

#if !defined(PL_HAVE_DOVI) || !PL_HAVE_DOVI
#error "The initial Linux libplacebo build must include built-in DOVI support"
#endif

#elif defined(Q_OS_MACOS)

#if !defined(PL_HAVE_VULKAN) || !PL_HAVE_VULKAN
#error "The macOS libplacebo build must include Vulkan"
#endif

#if !defined(PL_HAVE_SHADERC) || !PL_HAVE_SHADERC
#error "The macOS libplacebo build must include Shaderc"
#endif

#if !defined(PL_HAVE_DOVI) || !PL_HAVE_DOVI
#error "The macOS libplacebo build must include built-in DOVI support"
#endif

#if defined(PL_HAVE_LIBDOVI) && PL_HAVE_LIBDOVI
#error "The macOS libplacebo build must not depend on libdovi"
#endif

#if defined(PL_HAVE_D3D11) && PL_HAVE_D3D11
#error "The macOS libplacebo build must not include D3D11"
#endif

#if defined(PL_HAVE_OPENGL) && PL_HAVE_OPENGL
#error "The macOS libplacebo build must not include OpenGL"
#endif

#else
#error "Define the required libplacebo features for this platform"
#endif

class LibplaceboDependencyTest final : public QObject {
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
    void pinnedFeatureSetAndLogLifecycle();
};

void LibplaceboDependencyTest::pinnedFeatureSetAndLogLifecycle() {
    QCOMPARE(PL_MAJOR_VER, 7);
    QCOMPARE(PL_API_VER, 360);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    QCOMPARE(PL_FIX_VER, 1);
    QCOMPARE(QString::fromLatin1(PL_VERSION), QStringLiteral("v7.360.1"));
#else
    QVERIFY(QString::fromLatin1(PL_VERSION).startsWith(
        QStringLiteral("v7.360.")));
#if defined(PL_HAVE_LCMS) && PL_HAVE_LCMS
    qInfo("System libplacebo LCMS support: enabled");
#else
    qInfo("System libplacebo LCMS support: disabled");
#endif
#endif

    pl_log log = pl_log_create(PL_API_VER, nullptr);
    QVERIFY(log);
    pl_log_destroy(&log);
    QVERIFY(!log);
}

QTEST_APPLESS_MAIN(LibplaceboDependencyTest)
#include "tst_LibplaceboDependency.moc"
