#include <QtTest>
#include <QImage>
#include <QPainter>
#include "items/regions/rasteritem.h"

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
    void redactBlurObscuresSharpDetail() {
        // What the viewer and an image export see: the small image stretched back over
        // the region with smooth sampling, the way RedactItem paints it.
        QImage src = checker(48, 48);
        QImage small = redactBlur(src);
        QCOMPARE(small.size(), QSize(4, 4));
        QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter painter(&out);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(out.rect()), small);
        painter.end();
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
        QCOMPARE(out.size(), QSize(1, 1));   // never empty
        QCOMPARE(out.pixelColor(0, 0), QColor(Qt::red));
    }
};

QTEST_MAIN(TestRaster)
#include "test_raster.moc"
