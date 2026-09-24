#include <QtTest>
#include <QImage>
#include "studiostyle.h"

using namespace eddy;

static StudioStyle colorStyle() {
    StudioStyle s;
    s.background = StudioStyle::Background::Color;
    s.color = QColor(0, 0, 255);
    s.padding = 10;
    s.radius = 10;
    s.shadow = 0;
    return s;
}

class TestStudioStyle : public QObject {
    Q_OBJECT
private slots:
    void offLeavesContentUntouched() {
        QImage content(40, 30, QImage::Format_ARGB32);
        content.fill(Qt::red);
        const StudioStyle off;
        QVERIFY(!off.active());
        QCOMPARE(renderStudioImage(content, off), content);
        QCOMPARE(studioLayout(content.size(), off).output, content.size());
    }

    void paddingScalesWithTheShorterSide() {
        const auto small = studioLayout(QSize(200, 100), colorStyle());
        QCOMPARE(small.output, QSize(220, 120));
        QCOMPARE(small.content, QRect(10, 10, 200, 100));
        const auto large = studioLayout(QSize(2000, 1000), colorStyle());
        QCOMPARE(large.output, QSize(2200, 1200));
    }

    void aspectGrowsTheBackgroundNeverTheContent() {
        StudioStyle s = colorStyle();
        s.aspect = QSize(16, 9);
        const auto wide = studioLayout(QSize(100, 100), s);   // 120x120 before ratio
        QCOMPARE(wide.output.height(), 120);
        QVERIFY(qAbs(double(wide.output.width()) / wide.output.height() - 16.0 / 9) < 0.02);
        QCOMPARE(wide.content.size(), QSize(100, 100));
        // Centred to within the one pixel the even origin may cost.
        QVERIFY(qAbs(wide.content.left() - (wide.output.width() - wide.content.right() - 1)) <= 2);
        s.aspect = QSize(9, 16);
        const auto tall = studioLayout(QSize(100, 100), s);
        QCOMPARE(tall.output.width(), 120);
        QVERIFY(tall.output.height() > tall.output.width());
    }

    void contentOriginIsAlwaysEven() {
        StudioStyle s = colorStyle();
        for (double padding = 0; padding <= 30; padding += 0.7) {
            s.padding = padding;
            for (QSize aspect : {QSize(), QSize(16, 9), QSize(9, 16)}) {
                s.aspect = aspect;
                const auto layout = studioLayout(QSize(200, 100), s);
                QVERIFY2(layout.content.x() % 2 == 0 && layout.content.y() % 2 == 0,
                         qPrintable(QStringLiteral("padding %1").arg(padding)));
                QVERIFY(QRect(QPoint(), layout.output).contains(layout.content));
            }
        }
    }

    void outputSizesAreEven() {
        StudioStyle s = colorStyle();
        s.padding = 3;
        const auto layout = studioLayout(QSize(101, 57), s);
        QCOMPARE(layout.output.width() % 2, 0);
        QCOMPARE(layout.output.height() % 2, 0);
    }

    void framesContentWithRoundedCorners() {
        QImage content(200, 100, QImage::Format_ARGB32);
        content.fill(Qt::red);
        const QImage out = renderStudioImage(content, colorStyle());
        QCOMPARE(out.size(), QSize(220, 120));
        QCOMPARE(out.pixelColor(2, 2), QColor(0, 0, 255));          // background
        QCOMPARE(out.pixelColor(110, 60), QColor(255, 0, 0));       // content centre
        // The content's corner is cut away (radius 10 units = 10 px here).
        QCOMPARE(out.pixelColor(11, 11), QColor(0, 0, 255));
        QCOMPARE(out.pixelColor(10 + 5, 10 + 50), QColor(255, 0, 0));
    }

    void keepsTheContentsOwnTransparency() {
        QImage content(200, 100, QImage::Format_ARGB32);
        content.fill(Qt::transparent);
        for (int y = 0; y < 100; ++y)
            for (int x = 100; x < 200; ++x) content.setPixelColor(x, y, Qt::red);
        const QImage out = renderStudioImage(content, colorStyle());
        QCOMPARE(out.pixelColor(10 + 50, 60), QColor(0, 0, 255));    // transparent half: background
        QCOMPARE(out.pixelColor(10 + 150, 60), QColor(255, 0, 0));
    }

    void shadowDarkensBelowTheContentOnly() {
        QImage content(200, 100, QImage::Format_ARGB32);
        content.fill(Qt::white);
        StudioStyle s = colorStyle();
        s.color = QColor(200, 200, 200);
        s.shadow = 100;
        const QImage out = renderStudioImage(content, s);
        const QColor corner = out.pixelColor(1, 1);
        const QColor below = out.pixelColor(110, 112);   // just under the content
        QVERIFY2(below.lightness() < corner.lightness() - 20,
                 qPrintable(QStringLiteral("%1 vs %2").arg(below.name(), corner.name())));
        QCOMPARE(out.pixelColor(110, 60), QColor(Qt::white));
    }

    void frameMaskCoversAllButTheRoundedContent() {
        StudioStyle s = colorStyle();   // 200x100 -> 220x120, radius 10 px
        const QImage mask = renderStudioFrameMask(QSize(200, 100), s);
        QCOMPARE(mask.size(), QSize(220, 120));
        QCOMPARE(qGray(mask.pixel(2, 2)), 255);       // background
        QCOMPARE(qGray(mask.pixel(110, 60)), 0);      // content
        QCOMPARE(qGray(mask.pixel(11, 11)), 255);     // rounded corner shows background
        const int edge = qGray(mask.pixel(12, 13));   // antialiased, neither extreme
        QVERIFY2(edge > 0 && edge < 255, qPrintable(QString::number(edge)));
    }

    void gradientRunsAlongItsAngle() {
        StudioStyle s = colorStyle();
        s.background = StudioStyle::Background::Gradient;
        s.color = Qt::black;
        s.color2 = Qt::white;
        s.gradientAngle = 0;
        s.shadow = 0;
        const QImage bg = renderStudioBackground(QSize(200, 100), s);
        QVERIFY(bg.pixelColor(1, 60).lightness() < 20);
        QVERIFY(bg.pixelColor(bg.width() - 2, 60).lightness() > 235);
    }

    void missingBackgroundImageFallsBackToColor() {
        StudioStyle s = colorStyle();
        s.background = StudioStyle::Background::Image;
        s.imagePath = QStringLiteral("/nonexistent/eddy-bg.png");
        const QImage bg = renderStudioBackground(QSize(100, 100), s);
        QCOMPARE(bg.pixelColor(1, 1), QColor(0, 0, 255));
    }
};

QTEST_GUILESS_MAIN(TestStudioStyle)
#include "test_studiostyle.moc"
