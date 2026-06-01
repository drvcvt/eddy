#include <QtTest>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QTemporaryDir>
#include "exporter.h"
#include "items/redactitem.h"

using namespace eddy;

class TestExporter : public QObject {
    Q_OBJECT
private slots:
    void exportMatchesNativeSizeAndComposites() {
        QImage bg(80, 60, QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::white);
        QGraphicsScene scene;
        scene.setSceneRect(0,0,80,60);
        auto *bgItem = scene.addPixmap(QPixmap::fromImage(bg));
        bgItem->setZValue(-1000);                                   // app puts bg below redact (-500)
        auto *r = new RedactItem(RedactMode::Blacken, bg, QRectF(10,10,20,20));
        scene.addItem(r);
        QImage out = renderToImage(scene, QSize(80,60));
        QCOMPARE(out.size(), QSize(80,60));
        QCOMPARE(out.pixelColor(20,20).alpha(), 255);               // redact covers (opaque)
        QVERIFY(out.pixelColor(20,20).red() < 30);                  // near-black fill
        QCOMPARE(out.pixelColor(70,50), QColor(Qt::white));         // bg elsewhere
    }
    void pngRoundTrips() {
        QImage img(4,4,QImage::Format_ARGB32); img.fill(Qt::green);
        QByteArray png = encodePng(img);
        QVERIFY(png.startsWith("\x89PNG"));
        QTemporaryDir dir;
        QString path = dir.filePath("o.png");
        auto res = writePng(img, path);
        QVERIFY(res.ok);
        QImage reread(path);
        QCOMPARE(reread.size(), QSize(4,4));
    }
};

QTEST_MAIN(TestExporter)
#include "test_exporter.moc"
