#include <QKeyEvent>
#include <QTest>

#include "app/WindowShortcut.h"

class WindowShortcutTest final : public QObject {
    Q_OBJECT

  private slots:
    void recognizesRelativeSeekKeys();
    void rejectsModifiedAndUnrelatedKeys();
};

void WindowShortcutTest::recognizesRelativeSeekKeys() {
    QKeyEvent const left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QKeyEvent const right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QKeyEvent const macLeft(QEvent::KeyPress, Qt::Key_Left, Qt::KeypadModifier);
    QKeyEvent const macRight(QEvent::KeyPress, Qt::Key_Right, Qt::KeypadModifier);
    QKeyEvent const repeatedRight(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, QString{}, true, 3);

    QCOMPARE(relativeSeekMilliseconds(left), -10'000);
    QCOMPARE(relativeSeekMilliseconds(right), 10'000);
    QCOMPARE(relativeSeekMilliseconds(macLeft), -10'000);
    QCOMPARE(relativeSeekMilliseconds(macRight), 10'000);
    QCOMPARE(relativeSeekMilliseconds(repeatedRight), 10'000);
}

void WindowShortcutTest::rejectsModifiedAndUnrelatedKeys() {
    QKeyEvent const shiftedLeft(QEvent::KeyPress, Qt::Key_Left, Qt::ShiftModifier | Qt::KeypadModifier);
    QKeyEvent const commandRight(QEvent::KeyPress, Qt::Key_Right, Qt::ControlModifier | Qt::KeypadModifier);
    QKeyEvent const space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);

    QVERIFY(!relativeSeekMilliseconds(shiftedLeft));
    QVERIFY(!relativeSeekMilliseconds(commandRight));
    QVERIFY(!relativeSeekMilliseconds(space));
}

QTEST_APPLESS_MAIN(WindowShortcutTest)

#include "tst_WindowShortcut.moc"
