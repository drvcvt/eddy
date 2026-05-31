#include <QtTest>
#include <QImage>
#include <QBuffer>
#include <QTemporaryFile>
#include "imageio.h"

using namespace eddy;

static QByteArray pngBytes(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QByteArray b; QBuffer buf(&b); buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return b;
}

class TestImageIo : public QObject {
    Q_OBJECT
private slots:
    void loadsValidPngBytes() {
        auto r = loadImageBytes(pngBytes(20, 10));
        QVERIFY(r.ok);
        QCOMPARE(r.image.width(), 20);
        QCOMPARE(r.image.height(), 10);
        QCOMPARE(r.image.format(), QImage::Format_ARGB32_Premultiplied);
    }
    void rejectsGarbage() {
        auto r = loadImageBytes(QByteArray("not an image"));
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }
    void loadsFromFile() {
        QTemporaryFile f("XXXXXX.png"); QVERIFY(f.open());
        f.write(pngBytes(8, 8)); f.flush();
        InputSpec spec; spec.kind = InputSpec::File; spec.path = f.fileName();
        auto r = loadInput(spec);
        QVERIFY(r.ok);
        QCOMPARE(r.image.width(), 8);
    }
    void missingFileErrors() {
        InputSpec spec; spec.kind = InputSpec::File; spec.path = "/no/such.png";
        auto r = loadInput(spec);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.error.contains("cannot open"));
    }
};

QTEST_GUILESS_MAIN(TestImageIo)
#include "test_imageio.moc"
