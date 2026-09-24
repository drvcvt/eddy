#include <QtTest>
#include <QGraphicsScene>
#include <QJsonDocument>
#include "exporter.h"
#include "projectcodec.h"
#include "items/arrowitem.h"
#include "items/ellipseitem.h"
#include "items/highlightitem.h"
#include "items/penpathitem.h"
#include "items/rectitem.h"
#include "items/redactitem.h"
#include "items/spotlightitem.h"
#include "items/textitem.h"

using namespace eddy;

static QImage background() {
    QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(40, 90, 160));
    for (int x = 0; x < 400; x += 20)
        for (int y = 0; y < 300; ++y) image.setPixelColor(x, y, Qt::white);
    return image;
}

// One of every annotation, styled away from the defaults.
static QList<QGraphicsItem *> everything(const QImage &bg) {
    auto *arrow = new ArrowItem(QPointF(10, 10), QPointF(120, 80));
    arrow->setStrokeColor(QColor("#00ff88")); arrow->setStrokeWidth(8); arrow->setPos(3, 4);
    auto *rect = new RectItem(QRectF(30, 40, 90, 60)); rect->setStrokeWidth(2);
    auto *ellipse = new EllipseItem(QRectF(150, 20, 70, 50)); ellipse->setStrokeColor(Qt::yellow);
    auto *highlight = new HighlightItem(QRectF(200, 150, 120, 30));
    auto *pen = new PenPathItem(QPointF(50, 200));
    for (int i = 1; i < 40; ++i) pen->addPoint(QPointF(50 + i * 3, 200 + (i % 7) * 4));
    pen->setStrokeWidth(3.5);
    auto *text = new TextItem(QStringLiteral("Step one\nthen two"), QColor("#ececec"), 22);
    QFont font = text->font(); font.setBold(true); font.setItalic(true); text->setFont(font);
    text->setTextWidth(160);
    text->setAlignment(Qt::AlignHCenter);
    text->setLabelStyle(TextLabelStyle::Filled);
    text->setPos(220, 60);
    auto *blur = new RedactItem(RedactMode::Blur, bg, QRectF(10, 220, 80, 50));
    auto *ocr = new RedactItem(RedactMode::OcrBlacken, bg, QRectF(100, 230, 120, 40));
    ocr->setPos(5, -5);
    ocr->setTextRects({QRectF(105, 235, 40, 12), QRectF(150, 250, 50, 10)});
    ocr->setDetecting(false);
    auto *spot = new SpotlightItem(QRectF(260, 180, 100, 80), QSizeF(bg.size()));
    spot->setSpotlightShape(SpotlightShape::Ellipse);
    spot->setIntensity(3);
    QList<QGraphicsItem *> items{spot, blur, ocr, arrow, rect, ellipse, highlight, pen, text};
    for (auto *item : items) item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    return items;
}

static QImage render(const QList<QGraphicsItem *> &items, const QImage &bg) {
    QGraphicsScene scene(0, 0, bg.width(), bg.height());
    scene.addPixmap(QPixmap::fromImage(bg))->setZValue(-1000);
    for (auto *item : items) scene.addItem(item);
    const QImage image = renderToImage(scene, bg.size());
    for (auto *item : items) scene.removeItem(item);
    return image;
}

class TestProjectCodec : public QObject {
    Q_OBJECT
private slots:
    void everyAnnotationRoundTripsAndRendersTheSame() {
        const QImage bg = background();
        const QList<QGraphicsItem *> original = everything(bg);
        const QJsonArray json = itemsToJson(original);
        QCOMPARE(json.size(), original.size());
        // Through text, as the manifest stores it.
        const QJsonArray parsed = QJsonDocument::fromJson(QJsonDocument(json).toJson()).array();
        QString error;
        const auto loaded = itemsFromJson(parsed, bg, bg.size(), &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->size(), original.size());
        const auto *text = dynamic_cast<TextItem *>(loaded->last());
        QVERIFY(text);
        QVERIFY(text->state() == dynamic_cast<TextItem *>(original.last())->state());
        const auto *ocr = dynamic_cast<RedactItem *>(loaded->at(2));
        QVERIFY(ocr && !ocr->isDetecting());
        QCOMPARE(ocr->textRects().size(), 2);
        QCOMPARE(ocr->pos(), QPointF(5, -5));
        const auto *pen = dynamic_cast<PenPathItem *>(loaded->at(7));
        QCOMPARE(pen->points(), dynamic_cast<PenPathItem *>(original.at(7))->points());
        for (int i = 0; i < loaded->size(); ++i) {
            QCOMPARE(loaded->at(i)->zValue(), original.at(i)->zValue());
            QVERIFY(loaded->at(i)->flags().testFlag(QGraphicsItem::ItemIsSelectable));
        }
        QCOMPARE(render(*loaded, bg), render(original, bg));
        qDeleteAll(original);
        qDeleteAll(*loaded);
    }

    void aProjectRoundTrips() {
        ProjectSnapshot p;
        p.kind = MediaKind::Video;
        p.asset = QStringLiteral("source.mp4");
        p.sourceName = QStringLiteral("clip.mp4");
        p.sha256 = QString(64, QLatin1Char('a'));
        p.assetSize = 12345;
        p.size = QSize(1920, 1080);
        p.durationMs = 20000;
        p.crop = QRect(100, 50, 800, 600);
        p.trimInMs = 1000;
        p.trimOutMs = 9000;
        p.positionMs = 4200;
        p.studio.style.background = StudioStyle::Background::Color;
        p.studio.zooms = {{1, 2000, 4000, 2.0, ZoomSegment::Target::Point, QPointF(500, 300),
                           ZoomSegment::Motion::Smooth}};
        p.exportSettings = exportPreset(ExportPreset::Small);
        p.items = QJsonArray{QJsonObject{{"type", "rect"}, {"pos", QJsonArray{0, 0}}, {"z", 0},
            {"color", "#ff3b30"}, {"width", 4}, {"rect", QJsonArray{1, 2, 3, 4}}}};
        QString error;
        const auto back = projectFromJson(QJsonDocument::fromJson(QJsonDocument(projectToJson(p)).toJson()).object(), &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->asset, p.asset);
        QCOMPARE(back->sha256, p.sha256);
        QCOMPARE(back->crop, p.crop);
        QCOMPARE(back->trimOutMs, p.trimOutMs);
        QCOMPARE(back->positionMs, p.positionMs);
        QVERIFY(back->studio == p.studio);
        QCOMPARE(back->exportSettings, p.exportSettings);
        QCOMPARE(back->items, p.items);
    }

    void rejectsWhatItCannotTrust() {
        const QImage bg = background();
        QString error;
        auto reject = [&](const QJsonObject &item) {
            const auto items = itemsFromJson(QJsonArray{item}, bg, bg.size(), &error);
            return !items && !error.isEmpty();
        };
        const QJsonObject rect{{"type", "rect"}, {"pos", QJsonArray{0, 0}}, {"z", 0},
                               {"color", "#ff3b30"}, {"width", 4}, {"rect", QJsonArray{1, 2, 3, 4}}};
        QVERIFY(itemsFromJson(QJsonArray{rect}, bg, bg.size(), &error));
        QJsonObject bad = rect; bad["type"] = "sticker";
        QVERIFY(reject(bad));
        bad = rect; bad["rect"] = QJsonArray{1, 2, 3};
        QVERIFY(reject(bad));
        bad = rect; bad["width"] = 1e308 * 10;   // becomes null in JSON, still rejected
        QVERIFY(reject(bad));
        bad = rect; bad["color"] = "not a colour";
        QVERIFY(reject(bad));
        QJsonArray points;
        for (int i = 0; i < 20001; ++i) points.append(QJsonArray{i % 100, i % 50});
        QVERIFY(reject(QJsonObject{{"type", "pen"}, {"pos", QJsonArray{0, 0}}, {"z", 0},
                                   {"color", "#ff3b30"}, {"width", 4}, {"points", points}}));
        // Projects: a newer version and a path outside the assets folder are refused.
        ProjectSnapshot p;
        p.asset = QStringLiteral("source.png");
        p.sha256 = QString(64, QLatin1Char('b'));
        p.size = QSize(10, 10);
        QJsonObject json = projectToJson(p);
        json["version"] = 2;
        QVERIFY(!projectFromJson(json, &error));
        json = projectToJson(p);
        json["source"] = QJsonObject{{"asset", "../escape.png"}, {"sha256", QString(64, QLatin1Char('b'))},
                                     {"size", 1}, {"name", "x"}};
        QVERIFY(!projectFromJson(json, &error));
    }
};

QTEST_MAIN(TestProjectCodec)
#include "test_projectcodec.moc"
