#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include "studiodocument.h"

using namespace eddy;

static const QSize kSource(1920, 1080);
static constexpr qint64 kDuration = 20000;

static StudioDocument everything() {
    StudioDocument d;
    d.style.background = StudioStyle::Background::Gradient;
    d.style.color = QColor("#5b4bdb");
    d.style.color2 = QColor("#ff7eb3");
    d.style.gradientAngle = 90;
    d.style.padding = 12.5;
    d.style.radius = 3;
    d.style.shadow = 40;
    d.style.aspect = QSize(9, 16);
    d.zooms = {{7, 1000, 3500, 2.0, ZoomSegment::Target::Point, QPointF(960.5, 400), ZoomSegment::Motion::Smooth},
               {9, 5000, 6000, 1.5, ZoomSegment::Target::Cursor, QPointF(10, 20), ZoomSegment::Motion::Instant}};
    d.fragments = {{0, 1.0, false}, {4000, 1.0, true}, {8000, 2.0, false}};
    d.keepZoomedIn = true;
    d.keepCenter = QPointF(800, 540);
    return d;
}

static bool rejects(const QJsonObject &json, qint64 duration = kDuration) {
    QString error;
    const auto doc = studioFromJson(json, duration, kSource, &error);
    return !doc && !error.isEmpty();
}

// Changes one entry of an array in a copy of the complete document.
template <typename Change>
static QJsonObject withEntry(const char *array, int index, Change change) {
    QJsonObject json = studioToJson(everything());
    QJsonArray entries = json[QLatin1String(array)].toArray();
    QJsonObject entry = entries[index].toObject();
    change(entry);
    entries[index] = entry;
    json[QLatin1String(array)] = entries;
    return json;
}

class TestStudioDocument : public QObject {
    Q_OBJECT
private slots:
    void roundTripKeepsEverything() {
        const StudioDocument original = everything();
        QString error;
        const auto direct = studioFromJson(studioToJson(original), kDuration, kSource, &error);
        QVERIFY2(direct, qPrintable(error));
        QVERIFY(*direct == original);
        // Through text as well, as a project file will store it.
        const QByteArray text = QJsonDocument(studioToJson(original)).toJson();
        const auto parsed = studioFromJson(QJsonDocument::fromJson(text).object(), kDuration, kSource, &error);
        QVERIFY2(parsed, qPrintable(error));
        QVERIFY(*parsed == original);
    }

    void aFreshDocumentRoundTrips() {
        const StudioDocument fresh;
        QString error;
        const auto doc = studioFromJson(studioToJson(fresh), kDuration, kSource, &error);
        QVERIFY2(doc, qPrintable(error));
        QVERIFY(*doc == fresh);
    }

    void onlyVersionAndStyleAreRequired() {
        QJsonObject json = studioToJson(everything());
        for (const char *key : {"zooms", "fragments", "keepZoomedIn"}) json.remove(QLatin1String(key));
        QString error;
        const auto doc = studioFromJson(json, kDuration, kSource, &error);
        QVERIFY2(doc, qPrintable(error));
        QVERIFY(doc->zooms.isEmpty() && doc->fragments.isEmpty());
        QVERIFY(!doc->keepZoomedIn);
        QCOMPARE(doc->keepCenter, QPointF(960, 540));   // the source centre
    }

    void writesACompactStableShape() {
        const QJsonObject json = studioToJson(everything());
        QCOMPARE(json["version"].toInt(), 1);
        QCOMPARE(json["style"]["background"].toString(), QStringLiteral("gradient"));
        QCOMPARE(json["style"]["aspect"].toArray(), (QJsonArray{9, 16}));
        QCOMPARE(json["zooms"][0]["motion"].toString(), QStringLiteral("smooth"));
        QCOMPARE(json["zooms"][1]["target"].toString(), QStringLiteral("cursor"));
        // Defaults are left out of fragments, which may number in the hundreds.
        const QJsonObject first = json["fragments"][0].toObject();
        QCOMPARE(first.keys(), QStringList{QStringLiteral("start")});
        QCOMPARE(json["fragments"][2]["speed"].toDouble(), 2.0);
        // The cursor is Boltsnap's, baked into the video; Eddy stores no choice about it.
        QVERIFY(!json.contains(QLatin1String("cursor")));
        QVERIFY(json["style"]["image"].isNull());
    }

    void rejectsOtherVersions() {
        QJsonObject json = studioToJson(everything());
        json["version"] = 2;
        QVERIFY(rejects(json));
        json.remove(QLatin1String("version"));
        QVERIFY(rejects(json));
    }

    void rejectsBadStyles() {
        auto withStyle = [](auto change) {
            QJsonObject json = studioToJson(everything());
            QJsonObject style = json["style"].toObject();
            change(style);
            json["style"] = style;
            return json;
        };
        QVERIFY(rejects(withStyle([](QJsonObject &s) { s["padding"] = 50; })));
        QVERIFY(rejects(withStyle([](QJsonObject &s) { s["color"] = QStringLiteral("not a colour"); })));
        QVERIFY(rejects(withStyle([](QJsonObject &s) { s["background"] = QStringLiteral("image"); })));   // no image path
        QVERIFY(rejects(withStyle([](QJsonObject &s) { s["aspect"] = QJsonArray{0, 9}; })));
        QVERIFY(rejects(withStyle([](QJsonObject &s) { s["angle"] = 45.5; })));
        QJsonObject json = studioToJson(everything());
        json.remove(QLatin1String("style"));
        QVERIFY(rejects(json));
    }

    void rejectsBadZooms() {
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["scale"] = 5.0; })));
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["end"] = 25000; })));        // past the video
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["end"] = 1000; })));         // empty
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["end"] = 5500; })));         // overlaps the next
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["start"] = 1000.5; })));     // not whole ms
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["point"] = QJsonArray{2000, 10}; })));
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["motion"] = QStringLiteral("bouncy"); })));
        QVERIFY(rejects(withEntry("zooms", 0, [](QJsonObject &z) { z["id"] = 9; })));            // used twice
        QJsonObject swapped = studioToJson(everything());
        QJsonArray zooms = swapped["zooms"].toArray();
        swapped["zooms"] = QJsonArray{zooms[1], zooms[0]};
        QVERIFY(rejects(swapped));
    }

    void rejectsBadFragments() {
        QVERIFY(rejects(withEntry("fragments", 0, [](QJsonObject &f) { f["start"] = 100; })));   // must start at 0
        QVERIFY(rejects(withEntry("fragments", 2, [](QJsonObject &f) { f["start"] = 4000; })));  // not increasing
        QVERIFY(rejects(withEntry("fragments", 2, [](QJsonObject &f) { f["start"] = 20000; }))); // at the end
        QVERIFY(rejects(withEntry("fragments", 2, [](QJsonObject &f) { f["speed"] = 8; })));
        QVERIFY(rejects(withEntry("fragments", 2, [](QJsonObject &f) { f["removed"] = 1; })));   // not a bool
        QJsonObject allCut = studioToJson(everything());
        allCut["fragments"] = QJsonArray{QJsonObject{{"start", 0}, {"removed", true}}};
        QVERIFY(rejects(allCut));
    }

    void rejectsBadKeepSettings() {
        QJsonObject json = studioToJson(everything());
        json["keepZoomedIn"] = QJsonObject{{"on", true}, {"center", QJsonArray{-1, 5}}};
        QVERIFY(rejects(json));
        json = studioToJson(everything());
        json["keepZoomedIn"] = QJsonObject{{"on", 1}};
        QVERIFY(rejects(json));
        json = studioToJson(everything());
        json["keepZoomedIn"] = true;
        QVERIFY(rejects(json));
    }

    void rejectsOversizedLists() {
        QJsonObject json = studioToJson(StudioDocument());
        QJsonArray zooms;
        for (int i = 0; i <= kStudioMaxItems; ++i)
            zooms.append(QJsonObject{{"id", i + 1}, {"start", i * 10}, {"end", i * 10 + 10}, {"scale", 2},
                                     {"target", "point"}, {"point", QJsonArray{1, 1}}, {"motion", "focused"}});
        json["zooms"] = zooms;
        QVERIFY(rejects(json));
    }

    void imagesHaveNoTimeline() {
        QString error;
        QVERIFY(studioFromJson(studioToJson(StudioDocument()), 0, kSource, &error));
        QVERIFY(rejects(studioToJson(everything()), 0));
    }

    void onlyZoomsNeedTheRenderPath() {
        StudioDocument d;
        QVERIFY(!d.timeVarying());
        d.fragments = {{0, 2.0, false}};
        d.keepZoomedIn = true;
        QVERIFY(!d.timeVarying());          // cuts, speed and a fixed crop stay in the filter graph
        d.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(1, 1), ZoomSegment::Motion::Focused}};
        QVERIFY(d.timeVarying());
    }
};

QTEST_GUILESS_MAIN(TestStudioDocument)
#include "test_studiodocument.moc"
