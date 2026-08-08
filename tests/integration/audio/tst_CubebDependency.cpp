#include <QtTest>

#include <cubeb/cubeb.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class CubebDependencyTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void publicCAbiLinksWithoutOpeningDevice();
};

void CubebDependencyTest::publicCAbiLinksWithoutOpeningDevice() {
    auto volatile initFunction = &cubeb_init;
    auto volatile positionFunction = &cubeb_stream_get_position;
    auto volatile latencyFunction = &cubeb_stream_get_latency;
    QVERIFY(initFunction != nullptr);
    QVERIFY(positionFunction != nullptr);
    QVERIFY(latencyFunction != nullptr);

#ifdef Q_OS_WIN
    const cubeb_backend_names backends = cubeb_get_backend_names();
    QStringList compiledBackends;
    for (std::size_t index = 0; index < backends.count; ++index) {
        QVERIFY(backends.names[index] != nullptr);
        compiledBackends.push_back(QString::fromLatin1(backends.names[index]));
    }
    QVERIFY2(compiledBackends.contains(QStringLiteral("wasapi")),
             qPrintable(QStringLiteral("Compiled cubeb backends: %1").arg(compiledBackends.join(','))));
#else
    auto volatile backendFunction = &cubeb_get_backend_id;
    QVERIFY(backendFunction != nullptr);
#endif
}

QTEST_APPLESS_MAIN(CubebDependencyTest)
#include "tst_CubebDependency.moc"
