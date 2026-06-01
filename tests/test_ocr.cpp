#include <QtTest>
#include "ocr.h"

using namespace eddy::ocr;

// Mirrors snaptext tests/ocr_core.rs SAMPLE_TSV (tabs are literal \t).
static QString sampleTsv() {
    return QString::fromLatin1(
        "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
        "1\t1\t0\t0\t0\t0\t0\t0\t160\t80\t-1\t\n"
        "2\t1\t1\t0\t0\t0\t10\t20\t100\t10\t-1\t\n"
        "3\t1\t1\t1\t0\t0\t10\t20\t100\t10\t-1\t\n"
        "4\t1\t1\t1\t1\t0\t10\t20\t100\t10\t-1\t\n"
        "5\t1\t1\t1\t1\t1\t10\t20\t30\t10\t96.3\tHello\n"
        "5\t1\t1\t1\t1\t2\t50\t20\t60\t10\t91.0\tworld\n"
        "4\t1\t1\t1\t2\t0\t12\t50\t70\t10\t-1\t\n"
        "5\t1\t1\t1\t2\t1\t12\t50\t35\t10\t88.5\tNext\n"
        "5\t1\t1\t1\t2\t2\t55\t50\t27\t10\t89.0\tline\n");
}

class TestOcr : public QObject {
    Q_OBJECT
private slots:
    void parsesTsvIntoTextLinesWordsAndRects() {
        OcrDocument doc; QString err;
        QVERIFY2(parseTesseractTsv(sampleTsv(), &doc, &err), qPrintable(err));
        QCOMPARE(doc.text, QString("Hello world\nNext line"));
        QCOMPARE(doc.lines.size(), 2);
        QCOMPARE(doc.lines.at(0).text, QString("Hello world"));
        QCOMPARE(doc.lines.at(0).rect, QRect(10, 20, 100, 10));
        QCOMPARE(doc.lines.at(1).text, QString("Next line"));
        QCOMPARE(doc.lines.at(1).rect, QRect(12, 50, 70, 10));
        QCOMPARE(doc.words.size(), 4);
        QCOMPARE(doc.words.at(0).text, QString("Hello"));
        QCOMPARE(doc.words.at(0).rect, QRect(10, 20, 30, 10));
        QVERIFY(doc.words.at(0).hasConfidence);
        QCOMPARE(doc.words.at(0).confidence, 96.3f);
    }
    void rejectsMalformedHeader() {
        OcrDocument doc; QString err;
        QVERIFY(!parseTesseractTsv(QString("foo\tbar\n1\t2\n"), &doc, &err));
        QVERIFY(!err.isEmpty());
    }
    void emptyInputIsEmptyDocument() {
        OcrDocument doc; QString err;
        QVERIFY(parseTesseractTsv(QString(), &doc, &err));
        QVERIFY(doc.words.isEmpty());
        QVERIFY(doc.lines.isEmpty());
    }
    void headerOnlyIsEmpty() {
        const QString header = QString::fromLatin1(
            "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n");
        OcrDocument doc; QString err;
        QVERIFY2(parseTesseractTsv(header, &doc, &err), qPrintable(err));
        QVERIFY(doc.words.isEmpty());
        QVERIFY(doc.lines.isEmpty());
        QVERIFY(doc.text.isEmpty());
    }
    void reportsWordsIntersectingARegion() {
        OcrDocument doc; QString err;
        QVERIFY(parseTesseractTsv(sampleTsv(), &doc, &err));
        const auto hit = doc.wordsIntersecting(QRect(35, 18, 40, 16));
        QStringList texts;
        for (const OcrWord *w : hit) texts << w->text;
        QCOMPARE(texts, QStringList() << "Hello" << "world");
    }
    void groupsTextRegionsForRedaction() {
        OcrDocument doc; QString err;
        QVERIFY(parseTesseractTsv(sampleTsv(), &doc, &err));
        const auto regions = doc.textRegionsIntersecting(QRect(0, 15, 150, 55), 3, 15);
        QCOMPARE(regions.size(), 1);
        QCOMPARE(regions.at(0), QRect(7, 17, 106, 46));
        // A gap (14) larger than mergeLineGap keeps the two lines separate (else-branch).
        const auto split = doc.textRegionsIntersecting(QRect(0, 15, 150, 55), 3, 10);
        QCOMPARE(split.size(), 2);
    }
    void returnsEmptyWhenNothingIntersects() {
        OcrDocument doc; QString err;
        QVERIFY(parseTesseractTsv(sampleTsv(), &doc, &err));
        QVERIFY(doc.wordsIntersecting(QRect(0, 0, 1, 1)).isEmpty());
        QVERIFY(doc.textRegionsIntersecting(QRect(0, 0, 1, 1), 3, 15).isEmpty());
    }
    void mapsRectsBackToSourceCoords() {
        OcrDocument src;
        OcrWord w; w.text = "X"; w.rect = QRect(10, 20, 30, 10); src.words.append(w);
        OcrLine l; l.text = "X"; l.rect = QRect(10, 20, 30, 10); src.lines.append(l);

        const OcrDocument t1 = mapToSourceCoords(src, QPoint(100, 50), 1);
        QCOMPARE(t1.words.at(0).rect, QRect(110, 70, 30, 10));
        QCOMPARE(t1.lines.at(0).rect, QRect(110, 70, 30, 10));

        const OcrDocument t2 = mapToSourceCoords(src, QPoint(100, 50), 2);
        QCOMPARE(t2.words.at(0).rect, QRect(105, 60, 15, 5));
        QCOMPARE(t2.lines.at(0).rect, QRect(105, 60, 15, 5));
    }
    void choosesUpscaleForSmallCropsOnly() {
        QCOMPARE(chooseScale(QSize(200, 100)), 2);
        QCOMPARE(chooseScale(QSize(2000, 100)), 1);
        QCOMPARE(chooseScale(QSize(0, 0)), 1);
        QCOMPARE(chooseScale(QSize(1600, 1)), 2);
        QCOMPARE(chooseScale(QSize(1601, 1)), 1);
    }
    void resolvesTessdataDirInPriorityOrder() {
        const auto always = [](const QString &) { return true; };
        QCOMPARE(chooseTessdataDir("/explicit", "/env", "/home/me", "deu", always),
                 QString("/explicit"));

        const auto sysOrLocalDeu = [](const QString &p) {
            return p == "/usr/share/tessdata/deu.traineddata"
                || p == "/home/me/.local/share/tessdata/deu.traineddata";
        };
        QCOMPARE(chooseTessdataDir("", "", "/home/me", "deu", sysOrLocalDeu),
                 QString("/usr/share/tessdata"));

        const auto onlyLocalHasEng = [](const QString &p) {
            return p == "/usr/share/tessdata/deu.traineddata"
                || p == "/home/me/.local/share/tessdata/deu.traineddata"
                || p == "/home/me/.local/share/tessdata/eng.traineddata";
        };
        QCOMPARE(chooseTessdataDir("", "", "/home/me", "deu+eng", onlyLocalHasEng),
                 QString("/home/me/.local/share/tessdata"));

        const auto noneHasLang = [](const QString &) { return false; };
        QCOMPARE(chooseTessdataDir("", "", "/home/me", "deu", noneHasLang), QString());
    }
};

QTEST_MAIN(TestOcr)
#include "test_ocr.moc"
