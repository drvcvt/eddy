#include <QtTest>
#include <QApplication>
#include <QWheelEvent>
#include "minimap.h"

using namespace eddy;

class TestMiniMap : public QObject {
    Q_OBJECT
private slots:
    void sizeFollowsTheContent() {
        MiniMap map;
        map.setContent(QImage(), QRectF(0, 0, 1520, 760));
        QCOMPARE(map.size(), QSize(160, 84));
        map.setContent(QImage(), QRectF(0, 0, 1080, 1920));
        QCOMPARE(map.height(), 120);
        // Small content, small map.
        map.setContent(QImage(), QRectF(0, 0, 1520, 760));
        map.setWidthLimit(76);
        QCOMPARE(map.size(), QSize(84, 46));
    }
    void draggingMovesTheWindowByDocumentPixels() {
        MiniMap map;
        QPalette palette = map.palette();
        palette.setColor(QPalette::Window, Qt::white);
        map.setPalette(palette);
        QImage black(1520, 760, QImage::Format_RGB32);
        black.fill(Qt::black);
        map.setContent(black, QRectF(0, 0, 1520, 760));
        map.setCamera(QRectF(380, 190, 760, 380));
        map.show();
        QSignalSpy dragged(&map, &MiniMap::dragged);
        QSignalSpy finished(&map, &MiniMap::dragFinished);
        QTest::mousePress(&map, Qt::LeftButton, Qt::NoModifier, QPoint(80, 42));
        QTest::mouseMove(&map, QPoint(90, 42));
        QTest::mouseRelease(&map, Qt::LeftButton, Qt::NoModifier, QPoint(90, 42));
        QPointF total;
        for (const auto &args : dragged) total += args.first().toPointF();
        QVERIFY(qAbs(total.x() - 100) < 0.5 && qAbs(total.y()) < 0.5);   // 10 px of 152 across 1520
        QCOMPARE(finished.count(), 1);
        // The camera's window shows the picture, the rest is dimmed towards the surface.
        const QImage shot = map.grab().toImage();
        QVERIFY(qGray(shot.pixel(10, 10)) > qGray(shot.pixel(80, 42)) + 40);
    }
    void wheelZooms() {
        MiniMap map;
        map.setContent(QImage(), QRectF(0, 0, 1520, 760));
        QSignalSpy wheel(&map, &MiniMap::wheelZoom);
        QWheelEvent event(QPointF(80, 42), map.mapToGlobal(QPointF(80, 42)), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&map, &event);
        QCOMPARE(wheel.first().first().toDouble(), 1.0);
    }
};

QTEST_MAIN(TestMiniMap)
#include "test_minimap.moc"
