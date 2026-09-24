# Studio S0: Dokument, Zeitabbildung, Kamerafedern (Implementierungsplan)

> **Für die Umsetzung:** Task für Task, jede Task mit ihrem Test grün, bevor die nächste
> beginnt. Schritte als Checkboxen (`- [ ]`). **Keine Commits ohne ausdrücklichen Auftrag**;
> statt Commit-Schritten gibt es Checkpoints.

**Ziel:** Das speicherbare Studio-Modell (Zooms, Fragmente, Cursor-Wahl, keep zoomed in),
die Umrechnung Quellzeit ↔ Ausgabezeit und die vorab simulierte, gecachte Kamerafahrt als
reine, getestete Bausteine, noch ohne UI und ohne Änderung am Verhalten der App.

**Architektur:** Drei kleine Einheiten ohne Qt-Widgets: `studiodocument` (Daten + JSON mit
Validierung), `timemap` (Trim + Fragmente → Zeitabbildung), `camerapath` (kritisch gedämpfte
Feder, exakt pro 1/240-s-Schritt integriert, Hermite-Auswertung). Vorschau (S2) und Export
(S1) lesen später nur `CameraPath::rectAt()`. Grundlage: `docs/plans/2026-09-24-studio-features.md`,
Abschnitte 3.2 bis 3.4.

**Tech Stack:** C++20, Qt 6 Core/Gui (`QJsonObject`, `QRectF`), QtTest, CMake.

## Nachtrag 2026-09-24: Cursor entscheidet Boltsnap

Nach der Umsetzung entschieden: Eddy rendert den Cursor nicht neu. Deshalb entfernt, abweichend
von den Codeblöcken unten: `CursorRender`, `StudioDocument::cursor`, `Fragment::hideCursor`,
der JSON-Schlüssel `cursor`, die Tabellen `kModes`/`kSmoothings` und die dann unbenutzten
Rückfallwerte von `Reader::name`/`Reader::whole`. `timeVarying()` ist nur noch `!zooms.isEmpty()`.
Tests entsprechend: `rejectsBadKeepSettings` statt `rejectsBadCursorAndKeepSettings`,
`onlyZoomsNeedTheRenderPath`, und `writesACompactStableShape` prüft, dass kein `cursor` im JSON
steht. Maßgeblich ist der Code im Repo.

## Global Constraints

- Qt-Untergrenze: Linux-CI mit dem Distro-Qt 6 (`qt6-base-dev`), Windows-CI Qt 6.8.3, lokal
  Qt 6.11.1. Keine APIs, die nur in neueren Qt-Versionen existieren.
- Keine neuen Abhängigkeiten.
- Builds immer mit `cmake --build build-rel --parallel 2`.
- Tests über `eddy_test(<name>)` in `CMakeLists.txt`, `QTEST_GUILESS_MAIN`, Offscreen-Umgebung
  kommt aus CMake.
- Code und Kommentare Englisch, Kommentardichte wie im umgebenden Code (kurz, erklärt das
  Warum). Plan-Prosa Deutsch.
- Zeiten im Modell sind Quell-Millisekunden, Orte Dokumentpixel.
- Feder: Masse 1, kritisch gedämpft. Focused ω = 10 (Spannung 100, Reibung 20), Smooth ω = 6
  (36/12), Instant springt. Werte sind Vorschläge (N6), werden in S2 an Clips abgestimmt;
  deshalb stehen sie an genau einer Stelle (`cameraOmega`).
- Nichts committen, nichts pushen. Keine Subagents. `git diff --check` sauber.
- Die uncommittete Arbeit vom 2026-09-23 im Working Tree bleibt unangetastet, außer den hier
  genannten Einfügungen in `CMakeLists.txt`.

## Nicht in S0

- Keine UI, keine Änderung an `EditorWindow`, `Canvas`, `VideoTimeline`, Export.
- `ZoomSegment::Target::Cursor` wird gespeichert und validiert, die Kamera behandelt es in S0
  wie `Point` (Cursor-Folge kommt mit der Spur in S5).
- Kein Undo-Befehl (`SetStudioDocumentCommand` entsteht in S2, wenn er gebraucht wird).

## Dateien

| Datei | Verantwortung |
| --- | --- |
| `src/studiodocument.h/.cpp` (neu) | `ZoomSegment`, `Fragment`, `CursorRender`, `StudioDocument`, `timeVarying()`, `studioToJson`, `studioFromJson` |
| `src/timemap.h/.cpp` (neu) | `TimePiece`, `TimeMap` |
| `src/camerapath.h/.cpp` (neu) | `cameraOmega`, `springStep`, `CameraFrame`, `CameraPath` |
| `tests/test_studiodocument.cpp`, `tests/test_timemap.cpp`, `tests/test_camerapath.cpp` (neu) | Verhalten der drei Einheiten |
| `CMakeLists.txt` | drei Quellen in `eddy_core`, drei `eddy_test` |

---

### Task 1: Studio-Dokument und JSON-Format

**Files:**
- Create: `src/studiodocument.h`, `src/studiodocument.cpp`
- Create: `tests/test_studiodocument.cpp`
- Modify: `CMakeLists.txt` (Quellenliste von `eddy_core` nach `src/studiostyle.cpp`; Testliste nach `eddy_test(test_studiostyle)`)

**Interfaces:**
- Consumes: `StudioStyle` aus `src/studiostyle.h` (unverändert).
- Produces:
  - `struct ZoomSegment { quint32 id; qint64 startMs, endMs; double scale; enum class Target { Point, Cursor } target; QPointF point; enum class Motion { Focused, Smooth, Instant } motion; }`
  - `struct Fragment { qint64 startMs; double speed; bool removed; bool hideCursor; }`
  - `struct CursorRender { enum class Mode { Baked, Redraw, None } mode; double size; enum class Smoothing { Mellow, Quick, Raw } smoothing; int hideIdleMs; }`
  - `struct StudioDocument { StudioStyle style; QVector<ZoomSegment> zooms; QVector<Fragment> fragments; CursorRender cursor; bool keepZoomedIn; QPointF keepCenter; bool timeVarying() const; }`
  - `QJsonObject studioToJson(const StudioDocument &)`
  - `std::optional<StudioDocument> studioFromJson(const QJsonObject &, qint64 durationMs, QSize source, QString *error)`
  - `kStudioFormatVersion = 1`, `kStudioMaxItems = 1000`

- [x] **Step 1: Test schreiben**

`tests/test_studiodocument.cpp`:

```cpp
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
    d.fragments = {{0, 1.0, false, false}, {4000, 1.0, true, false}, {8000, 2.0, false, true}};
    d.cursor = {CursorRender::Mode::Redraw, 1.5, CursorRender::Smoothing::Quick, 2000};
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
        for (const char *key : {"zooms", "fragments", "cursor", "keepZoomedIn"}) json.remove(QLatin1String(key));
        QString error;
        const auto doc = studioFromJson(json, kDuration, kSource, &error);
        QVERIFY2(doc, qPrintable(error));
        QVERIFY(doc->zooms.isEmpty() && doc->fragments.isEmpty());
        QVERIFY(doc->cursor == CursorRender());
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
        QVERIFY(json["fragments"][2]["hideCursor"].toBool());
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

    void rejectsBadCursorAndKeepSettings() {
        QJsonObject json = studioToJson(everything());
        json["cursor"] = QJsonObject{{"mode", "ghost"}};
        QVERIFY(rejects(json));
        json = studioToJson(everything());
        json["cursor"] = QJsonObject{{"size", 5}};
        QVERIFY(rejects(json));
        json = studioToJson(everything());
        json["keepZoomedIn"] = QJsonObject{{"on", true}, {"center", QJsonArray{-1, 5}}};
        QVERIFY(rejects(json));
        json = studioToJson(everything());
        json["cursor"] = QStringLiteral("redraw");
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

    void onlyZoomsAndRedrawNeedTheRenderPath() {
        StudioDocument d;
        QVERIFY(!d.timeVarying());
        d.fragments = {{0, 2.0, false, false}};
        d.keepZoomedIn = true;
        QVERIFY(!d.timeVarying());          // cuts, speed and a fixed crop stay in the filter graph
        d.cursor.mode = CursorRender::Mode::Redraw;
        QVERIFY(d.timeVarying());
        d = StudioDocument();
        d.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(1, 1), ZoomSegment::Motion::Focused}};
        QVERIFY(d.timeVarying());
    }
};

QTEST_GUILESS_MAIN(TestStudioDocument)
#include "test_studiodocument.moc"
```

In `CMakeLists.txt` nach `eddy_test(test_studiostyle)`:

```cmake
eddy_test(test_studiodocument)
```

- [x] **Step 2: Test laufen lassen, er muss scheitern**

Run: `cmake --build build-rel --parallel 2 --target test_studiodocument`
Expected: Build-Fehler `studiodocument.h: No such file or directory`.

- [x] **Step 3: Implementieren**

`src/studiodocument.h`:

```cpp
#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>
#include <optional>
#include "studiostyle.h"

namespace eddy {

// Time-varying Studio edits (docs/plans/2026-09-24-studio-features.md, 3.2).
// Times are source milliseconds, places are document pixels, so cutting or
// speeding up fragments never moves them.
struct ZoomSegment {
    quint32 id = 0;                  // stable across edits, for selection and undo
    qint64 startMs = 0, endMs = 0;   // end exclusive
    double scale = 2.0;              // 1.1 .. 4
    enum class Target { Point, Cursor } target = Target::Point;
    QPointF point;                   // centre of the zoomed view
    enum class Motion { Focused, Smooth, Instant } motion = Motion::Focused;
    bool operator==(const ZoomSegment &) const = default;
};

// Fragments partition the source; each runs until the next one starts.
struct Fragment {
    qint64 startMs = 0;
    double speed = 1.0;              // 0.25 .. 4
    bool removed = false;
    bool hideCursor = false;         // only with CursorRender::Mode::Redraw
    bool operator==(const Fragment &) const = default;
};

// The cursor in the output: Boltsnap's baked one (the default), redrawn from
// the track over the clean video, or none.
struct CursorRender {
    enum class Mode { Baked, Redraw, None } mode = Mode::Baked;
    double size = 1.0;               // 0.5 .. 3
    enum class Smoothing { Mellow, Quick, Raw } smoothing = Smoothing::Mellow;
    int hideIdleMs = 0;              // 0: never hide
    bool operator==(const CursorRender &) const = default;
};

struct StudioDocument {
    StudioStyle style;
    QVector<ZoomSegment> zooms;      // sorted by start, never overlapping
    QVector<Fragment> fragments;     // empty: the whole source at 1x
    CursorRender cursor;
    bool keepZoomedIn = false;
    QPointF keepCenter;              // where the narrower view sits without a cursor track

    // Whether an export needs the frame-render path instead of the filter graph.
    bool timeVarying() const;
    bool operator==(const StudioDocument &) const = default;
};

inline constexpr int kStudioFormatVersion = 1;
inline constexpr int kStudioMaxItems = 1000;

QJsonObject studioToJson(const StudioDocument &doc);
// The whole block or nothing: an unusable block is rejected with a reason,
// never repaired. `durationMs` (0 for images) and `source` bound the values.
std::optional<StudioDocument> studioFromJson(const QJsonObject &json, qint64 durationMs,
                                             QSize source, QString *error);

}
```

`src/studiodocument.cpp`:

```cpp
#include "studiodocument.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <utility>

namespace eddy {

bool StudioDocument::timeVarying() const {
    return !zooms.isEmpty() || cursor.mode == CursorRender::Mode::Redraw;
}

namespace {

template <typename E> using Name = std::pair<const char *, E>;
using B = StudioStyle::Background;
using Target = ZoomSegment::Target;
using Motion = ZoomSegment::Motion;
using Mode = CursorRender::Mode;
using Smoothing = CursorRender::Smoothing;
constexpr Name<B> kBackgrounds[] = {{"none", B::None}, {"color", B::Color},
                                    {"gradient", B::Gradient}, {"image", B::Image}};
constexpr Name<Target> kTargets[] = {{"point", Target::Point}, {"cursor", Target::Cursor}};
constexpr Name<Motion> kMotions[] = {{"focused", Motion::Focused}, {"smooth", Motion::Smooth},
                                     {"instant", Motion::Instant}};
constexpr Name<Mode> kModes[] = {{"baked", Mode::Baked}, {"redraw", Mode::Redraw}, {"none", Mode::None}};
constexpr Name<Smoothing> kSmoothings[] = {{"mellow", Smoothing::Mellow}, {"quick", Smoothing::Quick},
                                           {"raw", Smoothing::Raw}};

template <typename E, std::size_t N>
QString nameOf(const Name<E> (&table)[N], E value) {
    for (const auto &[name, e] : table)
        if (e == value) return QString::fromLatin1(name);
    return QString::fromLatin1(table[0].first);
}

// Reads one block and keeps the first reason it is unusable.
struct Reader {
    QString error;

    bool fail(const QString &why) {
        if (error.isEmpty()) error = why;
        return false;
    }
    bool object(const QJsonObject &o, const char *key, QJsonObject *out) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isObject()) return fail(QStringLiteral("%1 must be an object").arg(QLatin1String(key)));
        *out = v.toObject();
        return true;
    }
    bool number(const QJsonObject &o, const char *key, double lo, double hi, double *out,
                std::optional<double> fallback = std::nullopt) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() && fallback) { *out = *fallback; return true; }
        const double d = v.toDouble(std::nan(""));
        if (!v.isDouble() || !std::isfinite(d) || d < lo || d > hi)
            return fail(QStringLiteral("%1 must be a number from %2 to %3")
                            .arg(QLatin1String(key)).arg(lo).arg(hi));
        *out = d;
        return true;
    }
    bool whole(const QJsonObject &o, const char *key, double lo, double hi, qint64 *out,
               std::optional<double> fallback = std::nullopt) {
        double d = 0;
        if (!number(o, key, lo, hi, &d, fallback)) return false;
        if (d != std::floor(d)) return fail(QStringLiteral("%1 must be a whole number").arg(QLatin1String(key)));
        *out = qint64(d);
        return true;
    }
    bool flag(const QJsonObject &o, const char *key, bool *out) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) { *out = false; return true; }
        if (!v.isBool()) return fail(QStringLiteral("%1 must be true or false").arg(QLatin1String(key)));
        *out = v.toBool();
        return true;
    }
    bool point(const QJsonObject &o, const char *key, QSize bounds, QPointF *out) {
        const QJsonArray a = o.value(QLatin1String(key)).toArray();
        const double x = a.size() == 2 ? a[0].toDouble(-1) : -1;
        const double y = a.size() == 2 ? a[1].toDouble(-1) : -1;
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0
            || x > bounds.width() || y > bounds.height())
            return fail(QStringLiteral("%1 must be [x, y] inside the video").arg(QLatin1String(key)));
        *out = QPointF(x, y);
        return true;
    }
    template <typename E, std::size_t N>
    bool name(const QJsonObject &o, const char *key, const Name<E> (&table)[N], E *out,
              std::optional<E> fallback = std::nullopt) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() && fallback) { *out = *fallback; return true; }
        for (const auto &[text, e] : table)
            if (v.toString() == QLatin1String(text)) { *out = e; return true; }
        return fail(QStringLiteral("unknown %1").arg(QLatin1String(key)));
    }

    bool style(const QJsonObject &o, StudioStyle *s) {
        if (!name(o, "background", kBackgrounds, &s->background)) return false;
        for (const auto &[key, color] : {std::pair{"color", &s->color}, std::pair{"color2", &s->color2}}) {
            const QColor c(o.value(QLatin1String(key)).toString());
            if (!c.isValid()) return fail(QStringLiteral("%1 is not a colour").arg(QLatin1String(key)));
            *color = c;
        }
        qint64 angle = 0;
        if (!whole(o, "angle", 0, 359, &angle) || !number(o, "padding", 0, 20, &s->padding)
            || !number(o, "radius", 0, 6, &s->radius) || !number(o, "shadow", 0, 100, &s->shadow))
            return false;
        s->gradientAngle = int(angle);
        const QJsonValue image = o.value(QLatin1String("image"));
        if (image.isString()) s->imagePath = image.toString();
        else if (!image.isNull() && !image.isUndefined()) return fail(QStringLiteral("image must be a path or null"));
        if (s->background == B::Image && s->imagePath.isEmpty())
            return fail(QStringLiteral("an image background needs an image"));
        const QJsonValue aspect = o.value(QLatin1String("aspect"));
        if (aspect.isArray()) {
            const QJsonArray a = aspect.toArray();
            const double w = a.size() == 2 ? a[0].toDouble(0) : 0, h = a.size() == 2 ? a[1].toDouble(0) : 0;
            if (w != std::floor(w) || h != std::floor(h) || w < 1 || h < 1 || w > 100 || h > 100)
                return fail(QStringLiteral("aspect must be two whole numbers from 1 to 100"));
            s->aspect = QSize(int(w), int(h));
        } else if (!aspect.isNull() && !aspect.isUndefined()) {
            return fail(QStringLiteral("aspect must be [w, h] or null"));
        }
        return true;
    }

    bool document(const QJsonObject &json, qint64 durationMs, QSize source, StudioDocument *d) {
        const QJsonValue version = json.value(QLatin1String("version"));
        if (!version.isDouble()) return fail(QStringLiteral("version is missing"));
        if (version.toDouble() != kStudioFormatVersion)
            return fail(QStringLiteral("unsupported Studio format version %1").arg(version.toDouble()));
        if (!json.value(QLatin1String("style")).isObject()) return fail(QStringLiteral("style is missing"));
        if (!style(json.value(QLatin1String("style")).toObject(), &d->style)) return false;

        const QJsonArray zooms = json.value(QLatin1String("zooms")).toArray();
        const QJsonArray fragments = json.value(QLatin1String("fragments")).toArray();
        if (zooms.size() > kStudioMaxItems || fragments.size() > kStudioMaxItems)
            return fail(QStringLiteral("more than %1 zooms or fragments").arg(kStudioMaxItems));
        if (durationMs <= 0 && (!zooms.isEmpty() || !fragments.isEmpty()))
            return fail(QStringLiteral("zooms and fragments need a video"));

        QSet<quint32> ids;
        qint64 lastEnd = 0;
        for (const QJsonValue &value : zooms) {
            const QJsonObject o = value.toObject();
            ZoomSegment z;
            qint64 id = 0;
            if (!whole(o, "id", 1, 4294967295.0, &id) || !whole(o, "start", 0, durationMs, &z.startMs)
                || !whole(o, "end", 0, durationMs, &z.endMs) || !number(o, "scale", 1.1, 4, &z.scale)
                || !name(o, "target", kTargets, &z.target) || !point(o, "point", source, &z.point)
                || !name(o, "motion", kMotions, &z.motion))
                return false;
            z.id = quint32(id);
            if (z.endMs <= z.startMs) return fail(QStringLiteral("a zoom ends before it starts"));
            if (z.startMs < lastEnd) return fail(QStringLiteral("zooms overlap or are out of order"));
            if (ids.contains(z.id)) return fail(QStringLiteral("zoom id %1 is used twice").arg(z.id));
            ids.insert(z.id);
            lastEnd = z.endMs;
            d->zooms.append(z);
        }

        bool kept = fragments.isEmpty();
        for (const QJsonValue &value : fragments) {
            const QJsonObject o = value.toObject();
            Fragment f;
            if (!whole(o, "start", 0, double(durationMs - 1), &f.startMs)
                || !number(o, "speed", 0.25, 4, &f.speed, 1.0) || !flag(o, "removed", &f.removed)
                || !flag(o, "hideCursor", &f.hideCursor))
                return false;
            if (d->fragments.isEmpty() ? f.startMs != 0 : f.startMs <= d->fragments.last().startMs)
                return fail(QStringLiteral("fragments must start at 0 and keep increasing"));
            kept = kept || !f.removed;
            d->fragments.append(f);
        }
        if (!kept) return fail(QStringLiteral("every fragment is cut"));

        QJsonObject cursor, keep;
        qint64 idle = 0;
        if (!object(json, "cursor", &cursor)
            || !name(cursor, "mode", kModes, &d->cursor.mode, std::optional(Mode::Baked))
            || !number(cursor, "size", 0.5, 3, &d->cursor.size, 1.0)
            || !name(cursor, "smoothing", kSmoothings, &d->cursor.smoothing, std::optional(Smoothing::Mellow))
            || !whole(cursor, "hideIdleMs", 0, 60000, &idle, 0.0))
            return false;
        d->cursor.hideIdleMs = int(idle);
        if (!object(json, "keepZoomedIn", &keep) || !flag(keep, "on", &d->keepZoomedIn)) return false;
        if (keep.contains(QLatin1String("center")) && !point(keep, "center", source, &d->keepCenter))
            return false;
        return true;
    }
};

}

QJsonObject studioToJson(const StudioDocument &doc) {
    const StudioStyle &s = doc.style;
    const QJsonObject style{
        {"background", nameOf(kBackgrounds, s.background)},
        {"color", s.color.name()},
        {"color2", s.color2.name()},
        {"angle", s.gradientAngle},
        {"image", s.imagePath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s.imagePath)},
        {"padding", s.padding},
        {"radius", s.radius},
        {"shadow", s.shadow},
        {"aspect", s.aspect.isEmpty() ? QJsonValue(QJsonValue::Null)
                                      : QJsonValue(QJsonArray{s.aspect.width(), s.aspect.height()})}};
    QJsonArray zooms;
    for (const ZoomSegment &z : doc.zooms)
        zooms.append(QJsonObject{
            {"id", qint64(z.id)}, {"start", z.startMs}, {"end", z.endMs}, {"scale", z.scale},
            {"target", nameOf(kTargets, z.target)}, {"point", QJsonArray{z.point.x(), z.point.y()}},
            {"motion", nameOf(kMotions, z.motion)}});
    QJsonArray fragments;
    for (const Fragment &f : doc.fragments) {
        QJsonObject o{{"start", f.startMs}};
        if (f.speed != 1.0) o[QLatin1String("speed")] = f.speed;
        if (f.removed) o[QLatin1String("removed")] = true;
        if (f.hideCursor) o[QLatin1String("hideCursor")] = true;
        fragments.append(o);
    }
    return QJsonObject{
        {"version", kStudioFormatVersion},
        {"style", style},
        {"zooms", zooms},
        {"fragments", fragments},
        {"cursor", QJsonObject{{"mode", nameOf(kModes, doc.cursor.mode)},
                               {"size", doc.cursor.size},
                               {"smoothing", nameOf(kSmoothings, doc.cursor.smoothing)},
                               {"hideIdleMs", doc.cursor.hideIdleMs}}},
        {"keepZoomedIn", QJsonObject{{"on", doc.keepZoomedIn},
                                     {"center", QJsonArray{doc.keepCenter.x(), doc.keepCenter.y()}}}}};
}

std::optional<StudioDocument> studioFromJson(const QJsonObject &json, qint64 durationMs,
                                             QSize source, QString *error) {
    Reader reader;
    StudioDocument doc;
    doc.keepCenter = QPointF(source.width() / 2.0, source.height() / 2.0);
    const bool ok = reader.document(json, durationMs, source, &doc);
    if (error) *error = ok ? QString() : reader.error;
    if (!ok) return std::nullopt;
    return doc;
}

}
```

In `CMakeLists.txt` in der Quellenliste von `eddy_core` nach `src/studiostyle.cpp`:

```cmake
    src/studiodocument.cpp
```

- [x] **Step 4: Test laufen lassen, er muss bestehen**

Run: `cmake --build build-rel --parallel 2 --target test_studiodocument && ctest --test-dir build-rel -R '^test_studiodocument$' --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [x] **Step 5: Checkpoint**

Run: `git diff --check && git status --short`
Expected: keine Ausgabe von `diff --check`; neu sind nur die drei Dateien, geändert zusätzlich `CMakeLists.txt`. Kein Commit.

---

### Task 2: Zeitabbildung (Trim und Fragmente)

**Files:**
- Create: `src/timemap.h`, `src/timemap.cpp`
- Create: `tests/test_timemap.cpp`
- Modify: `CMakeLists.txt` (`src/timemap.cpp` in `eddy_core`, `eddy_test(test_timemap)`)

**Interfaces:**
- Consumes: `Fragment` aus Task 1.
- Produces:
  - `struct TimePiece { double srcStart, srcEnd, outStart, speed; double outEnd() const; }`
  - `class TimeMap { TimeMap(); TimeMap(qint64 durationMs, qint64 trimInMs, qint64 trimOutMs, const QVector<Fragment> &); double outputDurationMs() const; std::optional<double> toOutput(double srcMs) const; double toOutputAfter(double srcMs) const; double toSource(double outMs) const; const QVector<TimePiece> &pieces() const; }`

- [x] **Step 1: Test schreiben**

`tests/test_timemap.cpp`:

```cpp
#include <QtTest>
#include "timemap.h"

using namespace eddy;

static Fragment part(qint64 start, double speed = 1, bool removed = false) {
    Fragment f;
    f.startMs = start;
    f.speed = speed;
    f.removed = removed;
    return f;
}

class TestTimeMap : public QObject {
    Q_OBJECT
private slots:
    void trimAloneShiftsTime() {
        const TimeMap map(10000, 2000, 8000, {});
        QCOMPARE(map.outputDurationMs(), 6000.0);
        QVERIFY(!map.toOutput(1999));
        QCOMPARE(*map.toOutput(2000), 0.0);
        QCOMPARE(*map.toOutput(5000), 3000.0);
        QCOMPARE(*map.toOutput(8000), 6000.0);   // the very end is still a place
        QVERIFY(!map.toOutput(8001));
        QCOMPARE(map.toSource(0), 2000.0);
        QCOMPARE(map.toSource(6000), 8000.0);
        QCOMPARE(map.toSource(-10), 2000.0);
        QCOMPARE(map.toOutputAfter(1000), 0.0);
        QCOMPARE(map.toOutputAfter(9000), 6000.0);
    }

    void cutsVanishAndSpeedCompresses() {
        const TimeMap map(10000, 0, 10000, {part(0), part(4000, 1, true), part(6000, 2)});
        QCOMPARE(map.pieces().size(), qsizetype(2));
        QCOMPARE(map.outputDurationMs(), 6000.0);
        QVERIFY(!map.toOutput(5000));
        QCOMPARE(map.toOutputAfter(5000), 4000.0);
        QCOMPARE(*map.toOutput(8000), 5000.0);
        QCOMPARE(*map.toOutput(10000), 6000.0);
        QCOMPARE(map.toSource(3999.5), 3999.5);
        QCOMPARE(map.toSource(4000), 6000.0);   // the seam belongs to the later piece
        QCOMPARE(map.toSource(5000), 8000.0);
    }

    void slowFragmentsStretchInsideTheTrim() {
        const TimeMap map(10000, 1000, 5000, {part(0), part(3000, 0.5)});
        QCOMPARE(map.outputDurationMs(), 6000.0);
        QCOMPARE(*map.toOutput(3000), 2000.0);
        QCOMPARE(*map.toOutput(4000), 4000.0);
        QCOMPARE(map.toSource(5000), 4500.0);
    }

    void roundTripsAndStaysMonotonic() {
        const TimeMap map(20000, 500, 19000, {part(0), part(3000, 1.5), part(7000, 1, true),
                                              part(9000, 0.25), part(10000, 4), part(15000)});
        for (double src = 0; src <= 20000; src += 7.3)
            if (const auto out = map.toOutput(src))
                QVERIFY2(qAbs(map.toSource(*out) - src) < 1e-9, qPrintable(QString::number(src)));
        double previous = -1;
        for (double out = 0; out <= map.outputDurationMs(); out += 3.1) {
            const double src = map.toSource(out);
            QVERIFY2(src >= previous, qPrintable(QString::number(out)));
            previous = src;
        }
    }

    void everythingCutLeavesNothing() {
        const TimeMap map(5000, 0, 5000, {part(0, 1, true)});
        QCOMPARE(map.outputDurationMs(), 0.0);
        QVERIFY(!map.toOutput(100));
        QCOMPARE(map.toSource(100), 0.0);
        QCOMPARE(map.toOutputAfter(100), 0.0);
    }
};

QTEST_GUILESS_MAIN(TestTimeMap)
#include "test_timemap.moc"
```

In `CMakeLists.txt` nach `eddy_test(test_studiodocument)`:

```cmake
eddy_test(test_timemap)
```

- [x] **Step 2: Test laufen lassen, er muss scheitern**

Run: `cmake --build build-rel --parallel 2 --target test_timemap`
Expected: Build-Fehler `timemap.h: No such file or directory`.

- [x] **Step 3: Implementieren**

`src/timemap.h`:

```cpp
#pragma once
#include <QVector>
#include <optional>
#include "studiodocument.h"

namespace eddy {

// One kept stretch of the source and where it lands in the output.
struct TimePiece {
    double srcStart = 0, srcEnd = 0;   // source ms, end exclusive
    double outStart = 0;               // output ms
    double speed = 1;
    double outEnd() const { return outStart + (srcEnd - srcStart) / speed; }
};

// Source time <-> output time for the trim plus fragments (studio plan 3.3).
// Every conversion lives here, none in UI code.
class TimeMap {
public:
    TimeMap() = default;
    TimeMap(qint64 durationMs, qint64 trimInMs, qint64 trimOutMs, const QVector<Fragment> &fragments);

    double outputDurationMs() const { return m_pieces.isEmpty() ? 0 : m_pieces.last().outEnd(); }
    // Empty where the source is cut or outside the trim.
    std::optional<double> toOutput(double srcMs) const;
    // Where `srcMs` lands, or the next kept source after it.
    double toOutputAfter(double srcMs) const;
    // Clamped to the output; monotonic and continuous across seams.
    double toSource(double outMs) const;
    const QVector<TimePiece> &pieces() const { return m_pieces; }

private:
    QVector<TimePiece> m_pieces;
};

}
```

`src/timemap.cpp`:

```cpp
#include "timemap.h"
#include <algorithm>

namespace eddy {

TimeMap::TimeMap(qint64 durationMs, qint64 trimInMs, qint64 trimOutMs,
                 const QVector<Fragment> &fragments) {
    const QVector<Fragment> parts = fragments.isEmpty() ? QVector<Fragment>{Fragment()} : fragments;
    const double in = std::clamp<double>(trimInMs, 0, durationMs);
    const double out = std::clamp<double>(trimOutMs, in, durationMs);
    double at = 0;
    for (int i = 0; i < parts.size(); ++i) {
        const double start = std::max<double>(parts[i].startMs, in);
        const double end = std::min<double>(i + 1 < parts.size() ? parts[i + 1].startMs : durationMs, out);
        if (parts[i].removed || end <= start) continue;
        m_pieces.append({start, end, at, parts[i].speed});
        at = m_pieces.last().outEnd();
    }
}

std::optional<double> TimeMap::toOutput(double srcMs) const {
    for (int i = 0; i < m_pieces.size(); ++i) {
        const TimePiece &p = m_pieces[i];
        const bool last = i == m_pieces.size() - 1;
        if (srcMs >= p.srcStart && (srcMs < p.srcEnd || (last && srcMs == p.srcEnd)))
            return p.outStart + (srcMs - p.srcStart) / p.speed;
    }
    return std::nullopt;
}

double TimeMap::toOutputAfter(double srcMs) const {
    for (const TimePiece &p : m_pieces) {
        if (srcMs < p.srcStart) return p.outStart;
        if (srcMs < p.srcEnd) return p.outStart + (srcMs - p.srcStart) / p.speed;
    }
    return outputDurationMs();
}

double TimeMap::toSource(double outMs) const {
    if (m_pieces.isEmpty()) return 0;
    const double t = std::clamp(outMs, 0.0, outputDurationMs());
    // The last piece starting at or before t, so a seam belongs to the later piece.
    const auto after = std::upper_bound(m_pieces.cbegin(), m_pieces.cend(), t,
        [](double value, const TimePiece &p) { return value < p.outStart; });
    const TimePiece &p = *(after == m_pieces.cbegin() ? after : after - 1);
    return std::min(p.srcEnd, p.srcStart + (t - p.outStart) * p.speed);
}

}
```

In `CMakeLists.txt` nach `src/studiodocument.cpp`:

```cmake
    src/timemap.cpp
```

- [x] **Step 4: Test laufen lassen, er muss bestehen**

Run: `cmake --build build-rel --parallel 2 --target test_timemap && ctest --test-dir build-rel -R '^test_timemap$' --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [x] **Step 5: Checkpoint**

Run: `git diff --check`
Expected: keine Ausgabe. Kein Commit.

---

### Task 3: Kamerafedern und gecachte Kamerafahrt

**Files:**
- Create: `src/camerapath.h`, `src/camerapath.cpp`
- Create: `tests/test_camerapath.cpp`
- Modify: `CMakeLists.txt` (`src/camerapath.cpp` in `eddy_core`, `eddy_test(test_camerapath)`)
- Modify: `docs/plans/2026-09-24-studio-features.md` (Abschnitt 9: S0 als umgesetzt markieren, erst nach grünem Gesamtlauf in Step 6)

**Interfaces:**
- Consumes: `ZoomSegment` (Task 1), `TimeMap::toOutputAfter`, `TimeMap::outputDurationMs` (Task 2).
- Produces (für S1 Export und S2 Vorschau):
  - `double cameraOmega(ZoomSegment::Motion)` (10, 6, 0 für Instant)
  - `void springStep(double omega, double target, double dt, double &x, double &v)`
  - `struct CameraFrame { QRectF content; double aspect; QPointF baseCenter; }`
  - `class CameraPath { static constexpr int kRate = 240; static constexpr double kJoinGapMs = 1000; CameraPath(); CameraPath(const QVector<ZoomSegment> &, const TimeMap &, const CameraFrame &); QRectF baseRect() const; QRectF rectAt(double outMs) const; int sampleCount() const; }`

- [x] **Step 1: Test schreiben**

`tests/test_camerapath.cpp`:

```cpp
#include <QtTest>
#include <cmath>
#include "camerapath.h"

using namespace eddy;

static const QRectF kContent(0, 0, 1920, 1080);

static ZoomSegment zoom(qint64 start, qint64 end, double scale, QPointF point,
                        ZoomSegment::Motion motion = ZoomSegment::Motion::Focused) {
    ZoomSegment z;
    z.id = quint32(start + 1);
    z.startMs = start;
    z.endMs = end;
    z.scale = scale;
    z.point = point;
    z.motion = motion;
    return z;
}

static CameraPath path(const QVector<ZoomSegment> &zooms, qint64 duration = 10000,
                       const QVector<Fragment> &fragments = {}) {
    return CameraPath(zooms, TimeMap(duration, 0, duration, fragments), CameraFrame{kContent, 0, {}});
}

// How far the camera is zoomed in, on the log scale the springs work in.
static double logZoom(const CameraPath &p, double outMs) {
    return std::log(p.baseRect().width() / p.rectAt(outMs).width());
}

static bool near(const QRectF &a, const QRectF &b, double tolerance = 1e-3) {
    return qAbs(a.left() - b.left()) <= tolerance && qAbs(a.top() - b.top()) <= tolerance
        && qAbs(a.width() - b.width()) <= tolerance && qAbs(a.height() - b.height()) <= tolerance;
}

class TestCameraPath : public QObject {
    Q_OBJECT
private slots:
    void springStepIsExact() {
        // Two half steps land where one full step does, and both match the closed form.
        double x1 = 3, v1 = -2, x2 = 3, v2 = -2;
        springStep(7, 1, 0.1, x1, v1);
        springStep(7, 1, 0.05, x2, v2);
        springStep(7, 1, 0.05, x2, v2);
        QVERIFY(qAbs(x1 - x2) < 1e-12 && qAbs(v1 - v2) < 1e-12);
        const double closed = 1 + (2 + (-2 + 7 * 2) * 0.1) * std::exp(-0.7);
        QVERIFY(qAbs(x1 - closed) < 1e-12);
    }

    void withoutZoomsTheCameraShowsEverything() {
        const CameraPath p = path({});
        for (double t : {0.0, 1234.5, 10000.0, 20000.0})
            QVERIFY(near(p.rectAt(t), kContent, 0));
    }

    void focusedSettlesInAboutSixTenthsOfASecond() {
        const CameraPath p = path({zoom(1000, 5000, 2, kContent.center())});
        const double target = std::log(2.0);
        QVERIFY(near(p.rectAt(999), kContent, 0));   // nothing moves before the zoom
        QVERIFY(near(p.rectAt(1000), kContent, 0));
        QVERIFY(qAbs(logZoom(p, 1500) - target) > 0.02 * target);
        QVERIFY(qAbs(logZoom(p, 1620) - target) < 0.02 * target);
    }

    void smoothTakesAboutASecond() {
        const CameraPath p = path({zoom(1000, 5000, 2, kContent.center(), ZoomSegment::Motion::Smooth)});
        const double target = std::log(2.0);
        QVERIFY(qAbs(logZoom(p, 1900) - target) > 0.02 * target);
        QVERIFY(qAbs(logZoom(p, 2000) - target) < 0.02 * target);
    }

    void instantJumpsExactlyBothWays() {
        const QPointF point(1400, 300);
        const CameraPath p = path({zoom(1000, 3000, 2, point, ZoomSegment::Motion::Instant)});
        QVERIFY(near(p.rectAt(999.9), kContent, 0));
        const QRectF zoomed = p.rectAt(1000);
        QVERIFY(near(zoomed, QRectF(point.x() - 480, point.y() - 270, 960, 540)));
        QVERIFY(near(p.rectAt(2999), zoomed));
        QVERIFY(near(p.rectAt(3000), kContent));
    }

    void neverShowsAnythingOutsideTheContent() {
        const CameraPath p = path({zoom(500, 2500, 4, QPointF(0, 0)),
                                   zoom(2600, 4000, 3, QPointF(1920, 1080), ZoomSegment::Motion::Smooth),
                                   zoom(6000, 7000, 1.1, QPointF(1919, 5))});
        const QRectF allowed = kContent.adjusted(-1e-3, -1e-3, 1e-3, 1e-3);
        for (double t = 0; t <= 10000; t += 1)
            QVERIFY2(allowed.contains(p.rectAt(t)), qPrintable(QString::number(t)));
    }

    void returnsToTheFullViewAfterTheLastZoom() {
        const CameraPath p = path({zoom(1000, 2000, 2, QPointF(300, 300))});
        QVERIFY(near(p.rectAt(4000), kContent, 0.01));
    }

    void sameInputGivesTheSameCurve() {
        const QVector<ZoomSegment> zooms{zoom(700, 3100, 2.5, QPointF(400, 900)),
                                         zoom(3300, 5000, 1.5, QPointF(1500, 200), ZoomSegment::Motion::Smooth)};
        const CameraPath a = path(zooms), b = path(zooms);
        for (int i = 0; i < 997; ++i) {
            const double t = std::fmod(i * 7919.0, 10000.0) + i * 0.001;
            const QRectF ra = a.rectAt(t), rb = b.rectAt(t);
            QVERIFY(ra.x() == rb.x() && ra.y() == rb.y() && ra.width() == rb.width() && ra.height() == rb.height());
        }
    }

    void closeZoomsHandOverWithoutZoomingOut() {
        const QPointF c = kContent.center();
        const CameraPath joined = path({zoom(1000, 2000, 2, c), zoom(2600, 3600, 2, c)});
        QVERIFY(logZoom(joined, 2300) > 0.95 * std::log(2.0));
        const CameraPath apart = path({zoom(1000, 2000, 2, c), zoom(5000, 6000, 2, c)});
        QVERIFY(logZoom(apart, 3500) < 0.05 * std::log(2.0));
    }

    void zoomsFollowFragments() {
        Fragment fast;
        fast.speed = 2;
        const CameraPath sped = path({zoom(2000, 6000, 2, QPointF(500, 500), ZoomSegment::Motion::Instant)},
                                     10000, {fast});
        QVERIFY(near(sped.rectAt(999), kContent, 0));
        QVERIFY(!near(sped.rectAt(1000), kContent));
        QVERIFY(!near(sped.rectAt(2999), kContent));
        QVERIFY(near(sped.rectAt(3000), kContent));
        Fragment kept, cut, after;
        cut.startMs = 2000;
        cut.removed = true;
        after.startMs = 5000;
        const CameraPath inCut = path({zoom(2500, 4500, 2, QPointF(500, 500))}, 10000, {kept, cut, after});
        for (double t = 0; t <= 7000; t += 25)
            QVERIFY(near(inCut.rectAt(t), kContent, 0));
    }

    void narrowOutputsShowTheLargestFittingWindow() {
        const TimeMap time(10000, 0, 10000, {});
        const CameraPath left({}, time, CameraFrame{QRectF(0, 0, 1600, 900), 9.0 / 16, QPointF(100, 450)});
        QVERIFY(near(left.baseRect(), QRectF(0, 0, 506.25, 900), 1e-9));
        const CameraPath middle({}, time, CameraFrame{QRectF(0, 0, 1600, 900), 9.0 / 16, QPointF(800, 450)});
        QVERIFY(near(middle.baseRect(), QRectF(546.875, 0, 506.25, 900), 1e-9));
        QVERIFY(near(middle.rectAt(5000), middle.baseRect()));
    }
};

QTEST_GUILESS_MAIN(TestCameraPath)
#include "test_camerapath.moc"
```

In `CMakeLists.txt` nach `eddy_test(test_timemap)`:

```cmake
eddy_test(test_camerapath)
```

- [x] **Step 2: Test laufen lassen, er muss scheitern**

Run: `cmake --build build-rel --parallel 2 --target test_camerapath`
Expected: Build-Fehler `camerapath.h: No such file or directory`.

- [x] **Step 3: Implementieren**

`src/camerapath.h`:

```cpp
#pragma once
#include <QPointF>
#include <QRectF>
#include <QVector>
#include "studiodocument.h"
#include "timemap.h"

namespace eddy {

// Critically damped spring with mass 1, the model Boltsnap uses for its
// cursor: stiffness omega^2, damping 2 omega. 0 means Instant, a jump.
double cameraOmega(ZoomSegment::Motion motion);
// Exact solution over `dt` seconds towards a fixed target: stable for any
// step and identical on every machine, unlike an Euler integrator.
void springStep(double omega, double target, double dt, double &x, double &v);

struct CameraFrame {
    QRectF content;      // document pixels the camera may show: the crop or the whole frame
    double aspect = 0;   // width/height of the output frame it fills; 0 = the content's own
    QPointF baseCenter;  // centre of the unzoomed view when `aspect` narrows it
};

// The camera over output time, simulated once at a fixed rate and cached, so
// preview and export read the same curve (studio plan 3.4). Rebuild it after
// every edit; it never integrates during playback.
class CameraPath {
public:
    static constexpr int kRate = 240;            // samples per output second
    static constexpr double kJoinGapMs = 1000;   // closer zooms hand over directly

    CameraPath() = default;
    CameraPath(const QVector<ZoomSegment> &zooms, const TimeMap &time, const CameraFrame &frame);

    QRectF baseRect() const { return m_base; }
    // Document pixels, always inside the frame's content.
    QRectF rectAt(double outMs) const;
    int sampleCount() const { return m_samples.size(); }

private:
    struct Sample {
        float x, y, logScale, vx, vy, vLogScale;
        bool jump;   // an Instant cut lands here: hold the previous sample until then
    };
    QRectF rectFor(double x, double y, double logScale) const;

    QVector<Sample> m_samples;
    QRectF m_content, m_base;
};

}
```

`src/camerapath.cpp`:

```cpp
#include "camerapath.h"
#include <algorithm>
#include <cmath>

namespace eddy {

double cameraOmega(ZoomSegment::Motion motion) {
    switch (motion) {
    case ZoomSegment::Motion::Focused: return 10.0;   // stiffness 100, damping 20: ~0.58 s to 2 %
    case ZoomSegment::Motion::Smooth: return 6.0;     // stiffness 36, damping 12: ~0.97 s
    case ZoomSegment::Motion::Instant: return 0.0;
    }
    return 10.0;
}

void springStep(double omega, double target, double dt, double &x, double &v) {
    const double x0 = x - target;
    const double b = v + omega * x0;
    const double e = std::exp(-omega * dt);
    x = target + (x0 + b * dt) * e;
    v = (v - omega * b * dt) * e;
}

// A centre that keeps a window of half-size `half` inside [lo, hi].
static double clampCentre(double centre, double half, double lo, double hi) {
    return hi - lo <= 2 * half ? (lo + hi) / 2 : std::clamp(centre, lo + half, hi - half);
}

CameraPath::CameraPath(const QVector<ZoomSegment> &zooms, const TimeMap &time, const CameraFrame &frame)
    : m_content(frame.content) {
    QSizeF baseSize = m_content.size();
    if (frame.aspect > 0) {
        const double w = std::min(m_content.width(), m_content.height() * frame.aspect);
        baseSize = QSizeF(w, w / frame.aspect);
    }
    const QPointF wanted = frame.aspect > 0 ? frame.baseCenter : m_content.center();
    m_base = QRectF(QPointF(), baseSize);          // rectFor sizes from m_base
    m_base = rectFor(wanted.x(), wanted.y(), 0);

    // Zooms in output time; cut ones vanish, close ones hand over directly.
    struct Span { double start, end; const ZoomSegment *zoom; };
    QVector<Span> spans;
    for (const ZoomSegment &z : zooms) {
        const double a = time.toOutputAfter(z.startMs), b = time.toOutputAfter(z.endMs);
        if (b > a) spans.append({a, b, &z});
    }
    for (int i = 0; i + 1 < spans.size(); ++i)
        if (spans[i + 1].start - spans[i].end < kJoinGapMs) spans[i].end = spans[i + 1].start;

    const int count = int(std::ceil(time.outputDurationMs() * kRate / 1000.0)) + 1;
    const double dt = 1.0 / kRate;
    const QPointF home = m_base.center();
    double x = home.x(), y = home.y(), ls = 0, vx = 0, vy = 0, vls = 0;
    double tx = x, ty = y, tls = 0;
    double omega = cameraOmega(ZoomSegment::Motion::Focused);
    int current = -1, next = 0;
    m_samples.reserve(count);
    for (int i = 0; i < count; ++i) {
        // The step into sample i follows the target that held before it, so a
        // zoom starts moving at its start, not one step early.
        if (i > 0) {
            springStep(omega, tx, dt, x, vx);
            springStep(omega, ty, dt, y, vy);
            springStep(omega, tls, dt, ls, vls);
        }
        const double t = i * 1000.0 / kRate;
        while (next < spans.size() && spans[next].end <= t) ++next;
        const int active = next < spans.size() && spans[next].start <= t ? next : -1;
        bool jump = false;
        if (active != current) {
            // Moving into a zoom uses its motion, moving out uses the one being left.
            omega = cameraOmega(spans[active >= 0 ? active : current].zoom->motion);
            current = active;
            if (active >= 0) {
                const ZoomSegment &z = *spans[active].zoom;
                tls = std::log(z.scale);
                tx = clampCentre(z.point.x(), m_base.width() / z.scale / 2, m_content.left(), m_content.right());
                ty = clampCentre(z.point.y(), m_base.height() / z.scale / 2, m_content.top(), m_content.bottom());
            } else {
                tx = home.x();
                ty = home.y();
                tls = 0;
            }
            if (omega == 0) {
                x = tx;
                y = ty;
                ls = tls;
                vx = vy = vls = 0;
                jump = true;
            }
        }
        m_samples.append({float(x), float(y), float(ls), float(vx), float(vy), float(vls), jump});
    }
}

QRectF CameraPath::rectFor(double x, double y, double logScale) const {
    const QSizeF size = m_base.size() / std::exp(std::max(0.0, logScale));
    return QRectF(QPointF(clampCentre(x, size.width() / 2, m_content.left(), m_content.right()) - size.width() / 2,
                          clampCentre(y, size.height() / 2, m_content.top(), m_content.bottom()) - size.height() / 2),
                  size);
}

QRectF CameraPath::rectAt(double outMs) const {
    if (m_samples.isEmpty()) return m_base;
    // The epsilon keeps a sample time from landing just below its own index.
    const double u = std::max(0.0, outMs) * kRate / 1000.0;
    const int i = int(std::floor(u + 1e-9));
    const Sample &a = m_samples[std::min(i, int(m_samples.size()) - 1)];
    if (i >= m_samples.size() - 1) return rectFor(a.x, a.y, a.logScale);
    const Sample &b = m_samples[i + 1];
    const double f = std::max(0.0, u - i);
    if (b.jump || f == 0) return rectFor(a.x, a.y, a.logScale);
    // Cubic Hermite with the spring's own velocities: smooth between samples.
    const double h = 1.0 / kRate;
    const double f2 = f * f, f3 = f2 * f;
    const double h00 = 2 * f3 - 3 * f2 + 1, h10 = f3 - 2 * f2 + f, h01 = -2 * f3 + 3 * f2, h11 = f3 - f2;
    auto blend = [&](double p0, double v0, double p1, double v1) {
        return h00 * p0 + h10 * h * v0 + h01 * p1 + h11 * h * v1;
    };
    return rectFor(blend(a.x, a.vx, b.x, b.vx), blend(a.y, a.vy, b.y, b.vy),
                   blend(a.logScale, a.vLogScale, b.logScale, b.vLogScale));
}

}
```

In `CMakeLists.txt` nach `src/timemap.cpp`:

```cmake
    src/camerapath.cpp
```

- [x] **Step 4: Test laufen lassen, er muss bestehen**

Run: `cmake --build build-rel --parallel 2 --target test_camerapath && ctest --test-dir build-rel -R '^test_camerapath$' --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [x] **Step 5: Gesamtlauf**

Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel && git diff --check`
Expected: Build grün, `100% tests passed, 0 tests failed out of 31` (28 bisherige plus drei neue), keine Ausgabe von `diff --check`. Hängt `test_crop` wieder im Watchdog: erst per gdb einen Backtrace ziehen (`gdb -batch -ex run -ex bt --args build-rel/test_crop`), dann weiter.

- [x] **Step 6: Plan-Stand nachtragen**

In `docs/plans/2026-09-24-studio-features.md`, Abschnitt 9, unter der Phasentabelle ergänzen:

```markdown
Umsetzungsstand: S0 umgesetzt (`studiodocument`, `timemap`, `camerapath`, drei Testsuiten),
Implementierungsplan `docs/plans/2026-09-24-studio-s0-model.md`. Noch nicht in der App verdrahtet.
```

- [x] **Step 7: Checkpoint**

Run: `git status --short && git diff --stat`
Expected: neu `src/studiodocument.*`, `src/timemap.*`, `src/camerapath.*`, drei Testdateien, dieser Plan; geändert `CMakeLists.txt` und `docs/plans/2026-09-24-studio-features.md`, sonst nur die vorher schon uncommitteten Dateien. Kein Commit; Stand melden und auf Auftrag warten.

---

## Abdeckung gegen den Gesamtplan

| Anforderung (Gesamtplan) | Task |
| --- | --- |
| 3.2 Datenmodell inkl. `hideCursor`, `CursorRender`, `keepZoomedIn` | 1 |
| 3.2 Speicherform, Validierung, Grenzen, Version | 1 |
| 4.1 `timeVarying()` entscheidet den Exportpfad | 1 |
| 3.3 `outputDuration`, `toOutput`, `toSource`, Trim als Außengrenze | 2 |
| 3.4 Feder exakt pro Schritt, 240 Hz, Hermite, Klemmung, Focused/Smooth/Instant, Übergabe < 1 s | 3 |
| 3.4 Tests: Einschwingzeit, nie außerhalb, Instant exakt, Determinismus, Vollbild vor/nach | 3 |
| 6.5 Basisausschnitt für schmalere Ausgabe (`CameraFrame::aspect`) | 3 |
| 6.6 Zooms folgen Schnitten und Speed | 3 |

Bewusst später: Undo-Befehl und UI (S2), Cursor-Folge (S5), Frame-Raster 60 fps im Export (S1).
