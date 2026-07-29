#include <QtTest>

#include "presentation/VideoPresentationGeometry.h"

class VideoPresentationGeometryTest final : public QObject {
    Q_OBJECT

private slots:
    void preservesViewportWithoutKnownAspectRatio();
    void pillarboxesWideViewport();
    void letterboxesTallViewport();
};

void VideoPresentationGeometryTest::
preservesViewportWithoutKnownAspectRatio() {
    const QRect viewport(10, 20, 640, 360);
    QCOMPARE(
        aspectFitVideoRect(viewport, std::nullopt),
        viewport);
    QCOMPARE(
        aspectFitVideoRect(viewport, 0.0),
        viewport);
}

void VideoPresentationGeometryTest::
pillarboxesWideViewport() {
    QCOMPARE(
        aspectFitVideoRect(
            QRect(0, 0, 1000, 500), 16.0 / 9.0),
        QRect(55, 0, 889, 500));
}

void VideoPresentationGeometryTest::
letterboxesTallViewport() {
    QCOMPARE(
        aspectFitVideoRect(
            QRect(20, 10, 640, 640), 16.0 / 9.0),
        QRect(20, 150, 640, 360));
}

QTEST_APPLESS_MAIN(VideoPresentationGeometryTest)
#include "tst_VideoPresentationGeometry.moc"
