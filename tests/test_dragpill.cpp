#include <QtTest>
#include <QMimeData>
#include <QImage>
#include <QUrl>
#include <QFileInfo>
#include <QFile>
#include <QLabel>
#include "dragpill.h"

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
    void constructsWithLabel() {
        DragPill pill;
        QVERIFY(pill.findChild<QLabel *>() != nullptr);
        QCOMPARE(pill.objectName(), QString("DragPill"));
    }
};

QTEST_MAIN(TestDragPill)
#include "test_dragpill.moc"
