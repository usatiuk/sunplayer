#include <QMimeData>
#include <QTest>

#include "app/LocalMediaDrop.h"

class LocalMediaDropTest final : public QObject {
    Q_OBJECT

  private slots:
    void acceptsOneLocalUrlWhenCopyIsAvailable();
    void acceptsUncUrlWithoutProbingIt();
    void rejectsUnsupportedPayloads();
};

void LocalMediaDropTest::acceptsOneLocalUrlWhenCopyIsAvailable() {
    QUrl const url = QUrl::fromLocalFile(QStringLiteral("C:/Media/film one.mkv"));
    QMimeData mimeData;
    mimeData.setUrls({url});

    QCOMPARE(singleLocalMediaDropUrl(&mimeData, Qt::CopyAction), url);
    QCOMPARE(singleLocalMediaDropUrl(&mimeData, Qt::CopyAction | Qt::MoveAction), url);
}

void LocalMediaDropTest::acceptsUncUrlWithoutProbingIt() {
    QUrl const url = QUrl::fromLocalFile(QStringLiteral("//server/share/film.mkv"));
    QMimeData mimeData;
    mimeData.setUrls({url});

    QCOMPARE(singleLocalMediaDropUrl(&mimeData, Qt::CopyAction), url);
}

void LocalMediaDropTest::rejectsUnsupportedPayloads() {
    QVERIFY(!singleLocalMediaDropUrl(nullptr, Qt::CopyAction));

    QMimeData noUrls;
    noUrls.setText(QStringLiteral("not a file"));
    QVERIFY(!singleLocalMediaDropUrl(&noUrls, Qt::CopyAction));

    QMimeData remoteUrl;
    remoteUrl.setUrls({QUrl(QStringLiteral("https://example.invalid/film.mkv"))});
    QVERIFY(!singleLocalMediaDropUrl(&remoteUrl, Qt::CopyAction));

    QMimeData emptyLocalPath;
    emptyLocalPath.setUrls({QUrl(QStringLiteral("file:"))});
    QVERIFY(!singleLocalMediaDropUrl(&emptyLocalPath, Qt::CopyAction));

    QMimeData multipleUrls;
    multipleUrls.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Media/one.mkv")),
                          QUrl::fromLocalFile(QStringLiteral("C:/Media/two.mkv"))});
    QVERIFY(!singleLocalMediaDropUrl(&multipleUrls, Qt::CopyAction));

    QMimeData moveOnly;
    moveOnly.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Media/film.mkv"))});
    QVERIFY(!singleLocalMediaDropUrl(&moveOnly, Qt::MoveAction));
}

QTEST_APPLESS_MAIN(LocalMediaDropTest)

#include "tst_LocalMediaDrop.moc"
