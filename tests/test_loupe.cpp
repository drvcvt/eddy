#include <QtTest>
#include <QImage>
#include <QColor>
#include "loupe.h"

using namespace eddy;

class TestLoupe : public QObject {
    Q_OBJECT
private slots:
    void cropRectCentred() {
        const QRect r = loupeCropRect(QPoint(10, 20), 15);
        QCOMPARE(r, QRect(3, 13, 15, 15));
        QCOMPARE(r.center(), QPoint(10, 20));        // odd cell count -> srcPos is the centre
    }
    void cropRectDefaultCells() {
        QCOMPARE(loupeCropRect(QPoint(0, 0)).size(), QSize(kLoupeCells, kLoupeCells));
    }
    void sampleInside() {
        QImage img(4, 4, QImage::Format_ARGB32);
        img.fill(Qt::black);
        img.setPixelColor(2, 1, QColor("#0a84ff"));
        QCOMPARE(loupeSampleColor(img, QPoint(2, 1)), QColor("#0a84ff"));
    }
    void sampleClampsOutOfRange() {
        QImage img(4, 4, QImage::Format_ARGB32);
        img.fill(Qt::black);
        img.setPixelColor(0, 0, QColor("#32d74b"));
        img.setPixelColor(3, 3, QColor("#ff9f0a"));
        QCOMPARE(loupeSampleColor(img, QPoint(-5, -5)), QColor("#32d74b"));   // clamps to (0,0)
        QCOMPARE(loupeSampleColor(img, QPoint(99, 99)), QColor("#ff9f0a"));   // clamps to (3,3)
    }
    void sampleNullImageIsBlack() {
        QCOMPARE(loupeSampleColor(QImage(), QPoint(0, 0)), QColor(Qt::black));
    }
    void widgetShowAtDoesNotCrash() {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::white);
        Loupe loupe;
        loupe.showAt(QPoint(4, 4), img, QPoint(4, 4));
        QVERIFY(loupe.width() > 0 && loupe.height() > 0);
    }
};

QTEST_MAIN(TestLoupe)
#include "test_loupe.moc"
