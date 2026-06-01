#include <QtTest>
#include <QImage>
#include "items/rasteritem.h"

using namespace eddy;

static QImage checker(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y=0;y<h;++y) for (int x=0;x<w;++x)
        img.setPixelColor(x,y, ((x/4 + y/4)%2) ? Qt::white : Qt::black);
    return img;
}

class TestRaster : public QObject {
    Q_OBJECT
private slots:
    void blurReducesLocalVariance() {
        QImage src = checker(40,40);
        QImage out = boxBlur(src, 5);
        QCOMPARE(out.size(), src.size());
        // center pixel of a blurred checker tends toward mid-gray
        QColor c = out.pixelColor(20,20);
        QVERIFY(c.red() > 40 && c.red() < 215);
    }
    void pixelateMakesBlocksUniform() {
        QImage src = checker(40,40);
        QImage out = pixelate(src, 8);
        // within one 8px block, all pixels equal
        QColor a = out.pixelColor(0,0), b = out.pixelColor(7,7);
        QCOMPARE(a, b);
    }
    void redactBlurObscuresSharpDetail() {
        QImage src = checker(48, 48);
        QImage out = redactBlur(src);
        QCOMPARE(out.size(), src.size());
        QVERIFY(out.pixelColor(24, 24) != src.pixelColor(24, 24));   // not a no-op
        // Heavy blur: a center pixel is no longer pure black/white...
        QColor c = out.pixelColor(24, 24);
        QVERIFY(c.red() > 30 && c.red() < 225);
        // ...and neighbouring pixels are similar (sharp edges destroyed).
        QColor c2 = out.pixelColor(25, 24);
        QVERIFY(qAbs(c.red() - c2.red()) < 20);   // strong blur: neighbours nearly identical
    }
    void redactBlurHandlesTinyImage() {
        QImage tiny(3, 3, QImage::Format_ARGB32);
        tiny.fill(Qt::red);
        QImage out = redactBlur(tiny);
        QCOMPARE(out.size(), tiny.size());
        QVERIFY(!out.isNull());
    }
};

QTEST_MAIN(TestRaster)
#include "test_raster.moc"
