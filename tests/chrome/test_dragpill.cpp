#include <QtTest>
#include <QApplication>
#include <QScopeGuard>
#include "app/theme.h"
#include "app/spring.h"
#include <QMimeData>
#include <QImage>
#include <QUrl>
#include <QFileInfo>
#include <QFile>
#include <QLabel>
#include <QPixmap>
#include "chrome/dragpill.h"

using namespace eddy;

class TestDragPill : public QObject {
    Q_OBJECT
private slots:
    void mimeCarriesImageAndFileUrl() {
        QImage img(12, 10, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QString path;
        QMimeData *mime = makeImageDropMime(img, &path);
        QVERIFY(mime->hasFormat("image/png"));           // explicit PNG bytes (foreign targets)
        QVERIFY(mime->data("image/png").startsWith("\x89PNG"));
        QVERIFY(mime->hasImage());                       // application/x-qt-image (Qt targets)
        QVERIFY(mime->hasUrls());                        // file-drop payload
        QVERIFY(!path.isEmpty());
        QVERIFY(path.endsWith(".png"));
        QVERIFY(QFileInfo::exists(path));
        QImage reread(path);
        QCOMPARE(reread.size(), QSize(12, 10));          // the temp file is the composite
        QCOMPARE(mime->urls().size(), 1);
        QCOMPARE(mime->urls().first().toLocalFile(), path);
        delete mime;
        QFile::remove(path);
    }
    void imageMimeKeepsPngBytesAfterTempRemoval() {
        QImage img(8, 6, QImage::Format_ARGB32);
        img.fill(Qt::blue);
        QString path;
        QMimeData *mime = makeImageDropMime(img, &path);
        QVERIFY(!path.isEmpty());
        QVERIFY(QFile::remove(path));

        QVERIFY(mime->hasFormat("image/png"));
        QVERIFY(mime->data("image/png").startsWith("\x89PNG"));
        QVERIFY(mime->hasImage());
        QVERIFY(mime->hasUrls());
        delete mime;
    }
    void fileMimeCarriesFileBackedBytesAndFileUrl() {
        QTemporaryFile f("XXXXXX.mp4");
        QVERIFY(f.open());
        const QByteArray bytes("fake video bytes");
        QCOMPARE(f.write(bytes), bytes.size());
        f.flush();

        QMimeData *mime = makeFileDropMime(f.fileName(), QStringLiteral("video/mp4"));
        QVERIFY(mime->hasFormat("video/mp4"));
        QCOMPARE(mime->data("video/mp4"), bytes);
        QVERIFY(mime->hasUrls());
        QCOMPARE(mime->urls().size(), 1);
        QCOMPARE(mime->urls().first().toLocalFile(), QFileInfo(f.fileName()).canonicalFilePath());
        delete mime;
    }
    void urlMimeCarriesOnlyFileUrl() {
        QTemporaryFile f("XXXXXX.mp4");
        QVERIFY(f.open());
        const QByteArray bytes("fake video bytes");
        QCOMPARE(f.write(bytes), bytes.size());
        f.flush();

        QMimeData *mime = makeUrlDropMime(f.fileName());
        QVERIFY(mime->hasUrls());
        QVERIFY(mime->hasFormat("text/uri-list"));
        QVERIFY(!mime->hasFormat("video/mp4"));
        QCOMPARE(mime->urls().size(), 1);
        QCOMPARE(mime->urls().first().toLocalFile(), QFileInfo(f.fileName()).canonicalFilePath());
        delete mime;
    }
    void constructsWithLabel() {
        DragPill pill;
        QVERIFY(pill.findChild<QLabel *>() != nullptr);
        QCOMPARE(pill.objectName(), QString("DragPill"));
    }
    void ghostPixmapIsTranslucentAndBounded() {
        QImage img(320, 180, QImage::Format_ARGB32);
        img.fill(QColor(10, 120, 240));

        const QPixmap ghost = makeDragGhostPixmap(img, QSize(180, 180));
        QVERIFY(!ghost.isNull());
        QVERIFY(ghost.width() <= 180);
        QVERIFY(ghost.height() <= 180);

        const QImage rendered = ghost.toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(rendered.pixelColor(0, 0).alpha() < 20);
        const QColor center = rendered.pixelColor(rendered.width() / 2, rendered.height() / 2);
        QVERIFY(center.alpha() > 80);
        QVERIFY(center.alpha() < 255);
    }
    void anExportFillsThePillAtOneWidth() {
        Spring::setEnabled(false);
        qApp->setPalette(theme::palette(true));
        qApp->setStyleSheet(theme::styleSheet(true));
        const auto plain = qScopeGuard([] { qApp->setStyleSheet({}); Spring::setEnabled(true); });
        DragPill pill;
        pill.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pill));
        const int width = pill.width();
        pill.setPreparationNeeded(true);
        QCOMPARE(pill.width(), width);
        pill.setEnabled(false);
        pill.setBusy(true);
        QCOMPARE(pill.width(), width);   // "Exporting" fits the reserved label
        QCOMPARE(pill.findChild<QLabel *>(QStringLiteral("DragPillText"))->text(), QStringLiteral("Exporting"));
        pill.setProgress(50);
        QCOMPARE(pill.progress(), 0.5);
        const QImage shot = pill.grab().toImage();
        const int filled = qGray(shot.pixel(pill.width() / 4, 2)), open = qGray(shot.pixel(pill.width() * 3 / 4, 2));
        QVERIFY2(filled > open, qPrintable(QStringLiteral("%1 %2").arg(filled).arg(open)));   // raise3 over the track
        pill.setBusy(false);
        QCOMPARE(pill.findChild<QLabel *>(QStringLiteral("DragPillText"))->text(), QStringLiteral("Prepare drag"));
    }
};

QTEST_MAIN(TestDragPill)
#include "test_dragpill.moc"
