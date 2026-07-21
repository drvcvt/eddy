#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QSignalSpy>
#include <QStandardPaths>
#include "redactocrcontroller.h"
#include "items/redactitem.h"
#include "ocr.h"

using namespace eddy;

class TestRedactOcrController : public QObject {
    Q_OBJECT
private slots:
    void applyResultNarrowsToDetectedText() {
        QImage src(80,80,QImage::Format_ARGB32); src.fill(Qt::white);
        RedactItem item(RedactMode::OcrBlacken, src, QRectF(10,10,60,60));
        item.setDetecting(true);
        RedactOcrController ctl(src, ocr::OcrOptions{});
        QSignalSpy changed(&ctl, SIGNAL(contentChanged()));
        QVERIFY(changed.isValid());

        ocr::OcrDocument doc;
        ocr::OcrLine line; line.text = "secret"; line.rect = QRect(20,20,30,10);
        doc.lines.append(line);

        const bool found = ctl.applyResult(&item, doc);
        QVERIFY(found);
        QVERIFY(!item.textRects().isEmpty());
        QVERIFY(!item.isDetecting());
        QCOMPARE(changed.count(), 1);
    }
    void applyResultWithNoIntersectingTextIsEmpty() {
        QImage src(80,80,QImage::Format_ARGB32); src.fill(Qt::white);
        RedactItem item(RedactMode::OcrBlacken, src, QRectF(10,10,20,20));   // region 10..30
        item.setDetecting(true);
        RedactOcrController ctl(src, ocr::OcrOptions{});

        ocr::OcrDocument doc;
        ocr::OcrLine line; line.text = "far"; line.rect = QRect(200,200,30,10);  // outside region
        doc.lines.append(line);

        const bool found = ctl.applyResult(&item, doc);
        QVERIFY(!found);
        QVERIFY(item.textRects().isEmpty());
        QVERIFY(!item.isDetecting());
    }
    void emptyRegionFailsSynchronouslyAndClearsDetecting() {
        QImage src(40,40,QImage::Format_ARGB32); src.fill(Qt::white);
        RedactItem item(RedactMode::OcrBlacken, src, QRectF(1000,1000,10,10));  // off-image
        RedactOcrController ctl(src, ocr::OcrOptions{});
        QSignalSpy failSpy(&ctl, &RedactOcrController::ocrFailed);
        ctl.detectFor(&item);                 // recognizeRegion emits failed() synchronously
        QCOMPARE(failSpy.count(), 1);         // failure reported
        QVERIFY(!item.isDetecting());         // detecting flag cleared by the failed handler
    }
    void detectsRealTextEndToEnd() {
        const bool required = qEnvironmentVariableIntValue("EDDY_REQUIRE_OCR") == 1;
        if (QStandardPaths::findExecutable("tesseract").isEmpty()) {
            if (required) QFAIL("tesseract is required by this test run");
            QSKIP("tesseract not installed");
        }
        QImage bg(420,140,QImage::Format_ARGB32);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.setPen(Qt::black);
        QFont f; f.setPointSize(32); p.setFont(f);
        p.drawText(bg.rect(), Qt::AlignCenter, "Hallo");
        p.end();

        RedactItem item(RedactMode::OcrBlacken, bg, QRectF(bg.rect()));
        RedactOcrController ctl(bg, ocr::OcrOptions{});
        QString failMsg;
        QObject::connect(&ctl, &RedactOcrController::ocrFailed,
                         &ctl, [&](const QString &m){ failMsg = m; });
        ctl.detectFor(&item);
        QTRY_VERIFY_WITH_TIMEOUT(!item.isDetecting(), 15000);
        if (!failMsg.isEmpty()) {
            if (required) QFAIL(qPrintable(failMsg));
            QSKIP(qPrintable("tesseract unavailable / language missing: " + failMsg));
        }
        QVERIFY(!item.textRects().isEmpty());                 // detected "Hallo" -> a cover rect
    }
    void applyResultMapsSceneRectsForMovedItem() {
        QImage src(120,120,QImage::Format_ARGB32); src.fill(Qt::white);
        RedactItem item(RedactMode::OcrBlacken, src, QRectF(0,0,40,40));
        item.setPos(50, 50);                          // scene region now (50,50,40,40)
        RedactOcrController ctl(src, ocr::OcrOptions{});
        ocr::OcrDocument doc;
        ocr::OcrLine line; line.text = "x"; line.rect = QRect(55,55,20,8);  // SCENE coords, inside moved region
        doc.lines.append(line);
        QVERIFY(ctl.applyResult(&item, doc));
        QVERIFY(!item.textRects().isEmpty());
        // stored in LOCAL frame (≈ scene - pos): left well below the scene x (55) → mapped
        QVERIFY(item.textRects().first().left() < 40);
    }
    void supersedingKeepsResultlessTargetCovered() {
        if (QStandardPaths::findExecutable("tesseract").isEmpty())
            QSKIP("tesseract not installed");      // detectFor launches the runner
        QImage bg(80,80,QImage::Format_ARGB32); bg.fill(Qt::white);
        RedactItem a(RedactMode::OcrBlacken, bg, QRectF(0,0,40,40));
        RedactItem b(RedactMode::OcrBlacken, bg, QRectF(40,40,40,40));
        RedactOcrController ctl(bg, ocr::OcrOptions{});
        ctl.detectFor(&a);                 // a: detecting, no results yet
        QVERIFY(a.isDetecting());
        ctl.detectFor(&b);                 // supersede a
        QVERIFY(a.isDetecting());          // a has no results -> stays covered (no exposure)
        QVERIFY(b.isDetecting());
        ctl.cancel();                      // stop tracking; don't act on async results in this test
    }
};

QTEST_MAIN(TestRedactOcrController)
#include "test_redactocrcontroller.moc"
