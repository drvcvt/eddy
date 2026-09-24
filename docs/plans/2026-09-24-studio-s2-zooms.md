# Studio S2: Zoom-Segmente in der App (Implementierungsplan)

> **Für die Umsetzung:** Task für Task, jede Task mit ihrem Test grün, bevor die nächste
> beginnt. Schritte als Checkboxen (`- [ ]`). Commits erst nach dem Review am Ende der Phase
> (Auftrag vom 2026-09-24: planen, umsetzen, reviewen, dann committen und pushen); statt
> Commit-Schritten gibt es Checkpoints.

**Ziel:** Zoom-Segmente, die man in der Timeline anlegt, im Canvas ausrichtet, in der
Vorschau mit echter Kamerafahrt sieht und über den Render-Pfad exportiert; dazu "Always keep
zoomed in" mit festem Mittelpunkt und die Camera-Seite im Studio-Popover.

**Architektur:** Das Modell aus S0 (`StudioDocument`, `TimeMap`, `CameraPath`) wird in
`EditorWindow` das eine Studio-Dokument (`m_studio`), geändert nur über
`SetStudioDocumentCommand` (ein Undo-Schritt pro Geste). Reine Logik für Einfügen, Ziehen und
Einrasten liegt in `zoomlane`, die Timeline zeichnet und bedient die Spur, der Canvas bekommt
die Kamera als zusätzlichen Anteil seiner View-Transformation (der Studio-Rahmen und die
eigene Ansicht bleiben stehen). Kontextleiste (`ZoomBar`), Mini-Karte (`MiniMap`) und die
Camera-Seite sind kleine eigene Widgets mit Signalen. Der Export bekommt die Zooms und den
"keep zoomed in"-Ausschnitt (`baseView`) über `VideoExportRequest`.
Grundlage: `docs/plans/2026-09-24-studio-features.md`, Abschnitte 3 bis 6.1, 6.5, 7, 11, 12.

**Tech Stack:** C++20, Qt 6 Widgets (QGraphicsView, QPainter, QUndoStack), QtTest, ffmpeg.

## Global Constraints

- Qt-Untergrenze: Linux-CI mit Distro-Qt 6, Windows-CI Qt 6.8.3, lokal 6.11.1. Keine neuen
  Abhängigkeiten. Builds mit `cmake --build build-rel --parallel 2`.
- Ohne Studio und ohne Zooms bleibt alles wie heute: gleiche Timeline-Höhe, gleiche
  Export-Argumente, alle bestehenden Tests grün.
- UI grau, Farbe nur im Export. Beschriftungen Englisch, Zahlen in der Mono-Schrift. Abstände
  aus der Skala {0, 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 72}; Leisten mit gleichem Innenrand
  (`chromeFollowsTheDensityRules`).
- Federn (N6, entschieden 2026-09-24 an neu gerenderten Clips): **Focused 100/20, Smooth
  36/12** (Omega 10 und 6), also die Werte, die S0 schon hat.
- Der Cursor ist Boltsnaps eingebrannter Cursor. S2 bietet kein Ziel "Cursor" an; Follow
  Cursor kommt mit S5.
- Zeitachse der Timeline bleibt in S2 die Quellzeit (ohne Fragmente ist sie gleich der
  Ausgabezeit plus Trim-Versatz); S4 stellt sie auf `TimeMap` um.
- Keine Subagents, `git diff --check` sauber.

## Entscheidungen für S2 (aus dem Gesamtplan abgeleitet, keine neuen Designfragen)

1. **Motion auf der Camera-Seite** setzt die Bewegung **aller** Zooms und ist der Standard für
   neue (`StudioDocument::motion`, im JSON als optionales `"camera": {"motion": …}`). Einzelne
   Zooms ändert die Kontextleiste. Haben die Zooms verschiedene Bewegungen, ist auf der Seite
   keine markiert.
2. **Kontextleiste** in S2: Zoomstufe (1.25×, 1.5×, 2×, 3×, freie Werte aus dem Mausrad
   erscheinen als Beschriftung), Motion (Menü mit den Kurvensymbolen), Remove. Point/Cursor
   kommt mit S5 dazu, damit S2 keinen toten Schalter zeigt. Drei Elemente passen auch in das
   schmale Fenster (520 px), ein Umbruch ist nicht nötig.
3. **Ziehen im Canvas** (Move-Werkzeug, pausiert, auf leerem Inhalt): mit gewähltem Zoom
   verschiebt es dessen Ziel; ohne gewählten Zoom und mit aktivem "keep zoomed in" den
   festen Mittelpunkt, und solange gezogen wird, zeigt der Canvas die Basisansicht. Auf einer
   Annotation bewegt es wie heute die Annotation.
4. **Mini-Karte** zeigt sich pausiert, wenn ein Zoom gewählt ist oder "keep zoomed in" wirkt.
   Sie sitzt unten rechts im Inhalt; überschneidet sie die Kontextleiste, rückt sie über sie.
5. **Canvas-Kamera:** Die Kamera wird aus einem gespeicherten "eigenen" Blick (Skalierung und
   Mitte ohne Kamera) plus Kamera-Rechteck berechnet. Rundung der Scrollbalken kann den
   Inhalt um höchstens 0,5 px gegen den Rahmen versetzen; der Rahmen selbst steht exakt.
6. **Mausrad über der Mini-Karte** ändert die Zoomstufe in Schritten von 10 % zwischen 1,1
   und 4; aufeinanderfolgende Raddrehungen am selben Zoom sind ein Undo-Schritt.
7. **Neue Zooms** bei `Z` am Playhead, per Klick in die leere Spur an der Klickstelle: 2 s
   (in die freie Lücke geschoben, Lücken unter 0,5 s lehnen mit Toast ab), 2×, Ziel = zuletzt
   benutzt oder Mitte der Basisansicht, Motion = Standard des Dokuments. Mindestlänge beim
   Ziehen 0,5 s.
8. **Tasten:** `Z` neuer Zoom; mit gewähltem Zoom und ohne gewählte Annotation `Delete` /
   `Backspace` entfernt, Pfeile links/rechts verschieben um ein Frame (Shift zehn), `Esc`
   bricht ein Ziehen ab bzw. hebt die Wahl auf, bevor es das Fenster schließt.
9. **Frame kopieren** (Ctrl+Shift+C) nimmt den sichtbaren Kameraausschnitt, also mit Zoom und
   "keep zoomed in", wie das Canvas ihn zeigt.

## Dateien

| Datei | Verantwortung |
| --- | --- |
| `src/studiodocument.h/.cpp` | `StudioDocument::motion`, JSON `camera.motion` |
| `src/camerapath.h/.cpp` | `targetRect`, `zoomAt` |
| `src/studiostyle.h/.cpp` | `keepZoomedInRect` |
| `src/undocommands.h` | `SetStudioDocumentCommand` (ersetzt `SetStudioStyleCommand`) |
| `src/zoomlane.h/.cpp` (neu) | Einfügen, Verschieben, Ränder, Einrasten |
| `src/videotimeline.h/.cpp` | Zoom-Spur: Zeichnen, Gesten, Signale |
| `src/canvas.h/.cpp` | Kamera, Rahmen ohne Kamera, Ziehen des Ziels |
| `src/motionicon.h/.cpp` (neu) | Motion-Symbole aus der Federfunktion |
| `src/zoombar.h/.cpp` (neu) | Kontextleiste |
| `src/minimap.h/.cpp` (neu) | Mini-Karte |
| `src/studiopopover.h/.cpp` | Seiten Style · Camera |
| `src/videoexporter.h/.cpp` | `VideoExportRequest::baseView` |
| `src/editorwindow.h/.cpp` | `m_studio`, Verdrahtung, Kamera-Vorschau, Tasten, Export |
| `resources/eddy.qss` | ZoomBar, MiniMap, Seiten-Tabs |
| `tools/eddy_preview.cpp` | Modi `studio-zoom`, `studio-camera` |
| Tests | `test_zoomlane`, `test_zoombar`, `test_minimap`, `test_studiopopover`, `test_studiozooms` (neu); Erweiterungen in `test_studiodocument`, `test_camerapath`, `test_studiostyle`, `test_videotimeline`, `test_canvas`, `test_videoexporter`, `test_editorwindow` |
| `CMakeLists.txt` | neue Quellen und Tests |

---

### Task 1: Modell-Ergänzungen und Undo-Befehl

**Files:**
- Modify: `src/studiodocument.h`, `src/studiodocument.cpp`, `src/camerapath.h`, `src/camerapath.cpp`,
  `src/studiostyle.h`, `src/studiostyle.cpp`, `src/undocommands.h`
- Test: `tests/test_studiodocument.cpp`, `tests/test_camerapath.cpp`, `tests/test_studiostyle.cpp`

**Interfaces:**
- Produces: `StudioDocument::motion` (`ZoomSegment::Motion`, Standard Focused);
  `QRectF CameraPath::targetRect(const ZoomSegment &) const`; `double CameraPath::zoomAt(double outMs) const`;
  `QRect keepZoomedInRect(QRect content, const StudioStyle &, QPointF center)`;
  `class SetStudioDocumentCommand(StudioDocument before, StudioDocument after, std::function<void(const StudioDocument &)> apply, int mergeKey = 0)`.

- [x] **Step 1: Tests schreiben.**

`tests/test_studiodocument.cpp`: in `everything()` nach `d.keepCenter = …` ergänzen
`d.motion = ZoomSegment::Motion::Smooth;`. In `onlyVersionAndStyleAreRequired` die Liste um
`"camera"` erweitern und am Ende prüfen:

```cpp
        QCOMPARE(doc->motion, ZoomSegment::Motion::Focused);
```

In `writesACompactStableShape` ergänzen:

```cpp
        QCOMPARE(json["camera"]["motion"].toString(), QStringLiteral("smooth"));
```

Neue Tests in derselben Klasse (Include `#include "undocommands.h"` und `#include <QUndoStack>` oben):

```cpp
    void rejectsAnUnknownCameraMotion() {
        QJsonObject json = studioToJson(everything());
        json["camera"] = QJsonObject{{"motion", "wobbly"}};
        QVERIFY(rejects(json));
        json["camera"] = 3;
        QVERIFY(rejects(json));
    }

    void documentCommandsUndoAndMergeWheelSteps() {
        StudioDocument current;
        QUndoStack stack;
        auto apply = [&](const StudioDocument &d) { current = d; };
        StudioDocument a = current, b = current, c = current;
        b.motion = ZoomSegment::Motion::Smooth;
        c.motion = ZoomSegment::Motion::Instant;
        stack.push(new SetStudioDocumentCommand(a, b, apply));
        QCOMPARE(current.motion, ZoomSegment::Motion::Smooth);
        // Wheel steps on one zoom fold into one undo step; other edits never do.
        stack.push(new SetStudioDocumentCommand(b, c, apply, 7));
        stack.push(new SetStudioDocumentCommand(c, b, apply, 7));
        QCOMPARE(stack.count(), 2);
        stack.push(new SetStudioDocumentCommand(b, c, apply, 8));
        QCOMPARE(stack.count(), 3);
        stack.undo();
        stack.undo();
        QCOMPARE(current.motion, ZoomSegment::Motion::Smooth);
        stack.undo();
        QCOMPARE(current.motion, ZoomSegment::Motion::Focused);
    }
```

`tests/test_camerapath.cpp`, neuer Test:

```cpp
    void targetRectIsWhereTheZoomSettles() {
        const ZoomSegment z = zoom(1000, 9000, 2.0, QPointF(1800, 100));
        const CameraPath p = path({z});
        // Clamped into the top-right quarter, like the camera itself.
        QVERIFY(near(p.targetRect(z), QRectF(960, 0, 960, 540), 1e-9));
        QVERIFY(near(p.rectAt(8000), p.targetRect(z), 0.5));
        QCOMPARE(p.zoomAt(0), 1.0);
        QVERIFY(qAbs(p.zoomAt(8000) - 2.0) < 1e-3);
    }
```

`tests/test_studiostyle.cpp`, neuer Test:

```cpp
    void keepZoomedInFillsTheFramesRatio() {
        StudioStyle s = colorStyle();                 // padding 10 %
        const QRect source(0, 0, 1920, 1080);
        QCOMPARE(keepZoomedInRect(source, s, QPointF(960, 540)), source);   // no ratio set
        s.aspect = QSize(9, 16);
        const QRect middle = keepZoomedInRect(source, s, QPointF(960, 540));
        QCOMPARE(middle.height(), 1080);
        QVERIFY(middle.width() < 1080);
        QCOMPARE(middle.x() % 2, 0);
        QCOMPARE(middle.width() % 2, 0);
        QVERIFY(qAbs(middle.center().x() - 960) <= 2);
        // The frame fills the ratio: at most 2 px of background grow beside the padding.
        const StudioLayout layout = studioLayout(middle.size(), s);
        QVERIFY2(layout.content.x() - layout.content.y() <= 2,
                 qPrintable(QStringLiteral("%1 vs %2").arg(layout.content.x()).arg(layout.content.y())));
        // The window never leaves the content.
        QCOMPARE(keepZoomedInRect(source, s, QPointF(0, 540)).x(), 0);
        const QRect right = keepZoomedInRect(source, s, QPointF(1920, 540));
        QCOMPARE(right.x() + right.width(), 1920);
        // Wide ratios cut the height instead; inside a crop the window stays inside it.
        s.aspect = QSize(21, 9);
        const QRect crop(100, 50, 800, 600);
        const QRect wide = keepZoomedInRect(crop, s, QPointF(500, 350));
        QCOMPARE(wide.width(), 800);
        QVERIFY(wide.height() < 600);
        QVERIFY(crop.contains(wide));
    }
```

- [x] **Step 2: Tests laufen lassen, sie scheitern am Build** (`motion`, `targetRect`, `zoomAt`,
  `keepZoomedInRect`, `SetStudioDocumentCommand` fehlen).

Run: `cmake --build build-rel --parallel 2 2>&1 | grep -m3 error`
Expected: Fehler über die fehlenden Namen.

- [x] **Step 3: Umsetzen.**

`src/studiodocument.h`, in `StudioDocument` nach `QPointF keepCenter;`:

```cpp
    // Motion for new zooms; the Camera page sets it and every zoom at once.
    ZoomSegment::Motion motion = ZoomSegment::Motion::Focused;
```

`src/studiodocument.cpp`, in `Reader::document` vor `QJsonObject keep;`:

```cpp
        QJsonObject camera;
        if (!object(json, "camera", &camera)) return false;
        if (camera.contains(QLatin1String("motion")) && !name(camera, "motion", kMotions, &d->motion))
            return false;
```

und in `studioToJson` nach `{"fragments", fragments},`:

```cpp
        {"camera", QJsonObject{{"motion", nameOf(kMotions, doc.motion)}}},
```

`src/camerapath.h`, öffentlich nach `rectAt`:

```cpp
    // Where a zoom's camera comes to rest: its window around the point, kept
    // inside the content. What the editor shows while that zoom is selected.
    QRectF targetRect(const ZoomSegment &zoom) const {
        return rectFor(zoom.point.x(), zoom.point.y(), std::log(zoom.scale));
    }
    // How far the camera is zoomed in at `outMs`; 1 is the base view.
    double zoomAt(double outMs) const {
        const QRectF r = rectAt(outMs);
        return r.width() > 0 ? m_base.width() / r.width() : 1.0;
    }
```

(`#include <cmath>` in `camerapath.h` ergänzen.)

`src/studiostyle.h` nach `studioLayout`:

```cpp
// "Always keep zoomed in" (studio plan 6.5): the largest window of `content`
// whose framed output has the style's ratio without background bars, centred
// as near `center` as it fits. Even origin and size, like any video crop.
// `content` itself when the style sets no ratio.
QRect keepZoomedInRect(QRect content, const StudioStyle &style, QPointF center);
```

`src/studiostyle.cpp` nach `studioLayout`:

```cpp
QRect keepZoomedInRect(QRect content, const StudioStyle &style, QPointF center) {
    if (!style.active() || style.aspect.isEmpty() || content.width() < 4 || content.height() < 4)
        return content;
    const double want = double(style.aspect.width()) / style.aspect.height();
    // The ratio of the padded window, before studioLayout grows it to `want`;
    // it rises with the width and falls with the height.
    auto framed = [&](int w, int h) {
        const int pad = qRound(std::clamp(style.padding, 0.0, 50.0) * unit(QSize(w, h)));
        return double(w + 2 * pad) / (h + 2 * pad);
    };
    int w = content.width() & ~1, h = content.height() & ~1;
    if (framed(w, h) > want) {
        int lo = 2, hi = w;   // the widest even window that is not too wide
        while (lo < hi) {
            const int mid = ((lo + hi + 2) / 2) & ~1;
            if (framed(mid, h) <= want) lo = mid; else hi = mid - 2;
        }
        w = lo;
    } else {
        int lo = 2, hi = h;   // the tallest even window that is not too tall
        while (lo < hi) {
            const int mid = ((lo + hi + 2) / 2) & ~1;
            if (framed(w, mid) >= want) lo = mid; else hi = mid - 2;
        }
        h = lo;
    }
    const int x = qBound(0, qRound(center.x() - w / 2.0) - content.x(), content.width() - w) & ~1;
    const int y = qBound(0, qRound(center.y() - h / 2.0) - content.y(), content.height() - h) & ~1;
    return QRect(content.x() + x, content.y() + y, w, h);
}
```

`src/undocommands.h`: `#include "studiodocument.h"` ergänzen und `SetStudioStyleCommand` ersetzen durch:

```cpp
// The whole Studio document before and after one gesture. Commands with the
// same non-zero `mergeKey` in a row fold into one step (mouse-wheel zooming).
class SetStudioDocumentCommand : public QUndoCommand {
public:
    using Apply = std::function<void(const StudioDocument &)>;
    SetStudioDocumentCommand(StudioDocument before, StudioDocument after, Apply apply, int mergeKey = 0)
        : QUndoCommand(QStringLiteral("Studio")), m_before(std::move(before)),
          m_after(std::move(after)), m_apply(std::move(apply)), m_key(mergeKey) {}
    void undo() override { m_apply(m_before); }
    void redo() override { m_apply(m_after); }
    int id() const override { return m_key ? 0x5354 : -1; }
    bool mergeWith(const QUndoCommand *other) override {
        const auto *next = static_cast<const SetStudioDocumentCommand *>(other);
        if (next->m_key != m_key) return false;
        m_after = next->m_after;
        return true;
    }
private:
    StudioDocument m_before, m_after;
    Apply m_apply;
    int m_key;
};
```

`src/editorwindow.cpp` benutzt `SetStudioStyleCommand` an einer Stelle (`openStudio`); bis
Task 9 dort übergangsweise:

```cpp
        StudioDocument before;
        before.style = styleBefore;
        StudioDocument after;
        after.style = m_studioStyle;
        m_undo->push(new SetStudioDocumentCommand(before, after,
            [this](const StudioDocument &d) { setStudioStyle(d.style); }));
```

(mit `styleBefore` als umbenanntem `before` im Lambda-Capture).

- [x] **Step 4: Build und Tests.**

Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R "studiodocument|camerapath|studiostyle|editorwindow" --output-on-failure`
Expected: alle bestanden.

---

### Task 2: Zoom-Spur-Logik

**Files:**
- Create: `src/zoomlane.h`, `src/zoomlane.cpp`, `tests/test_zoomlane.cpp`
- Modify: `CMakeLists.txt` (`src/zoomlane.cpp` nach `src/camerapath.cpp`, `eddy_test(test_zoomlane)` nach `eddy_test(test_camerapath)`)

**Interfaces:**
- Produces (namespace `eddy::zoomlane`): `kNewZoomMs = 2000`, `kMinZoomMs = 500`;
  `std::optional<std::pair<qint64, qint64>> placeNew(const QVector<ZoomSegment> &, qint64 timeMs, qint64 durationMs)`;
  `int insert(QVector<ZoomSegment> &, const ZoomSegment &)`; `quint32 nextId(const QVector<ZoomSegment> &)`;
  `int indexOf(const QVector<ZoomSegment> &, quint32 id)`;
  `void move(QVector<ZoomSegment> &, quint32 id, qint64 startMs, qint64 durationMs)`;
  `void resize(QVector<ZoomSegment> &, quint32 id, bool startEdge, qint64 timeMs, qint64 durationMs)`;
  `qint64 snap(qint64 timeMs, const QVector<qint64> &anchors, qint64 toleranceMs)`.

- [x] **Step 1: Test schreiben** `tests/test_zoomlane.cpp`:

```cpp
#include <QtTest>
#include "zoomlane.h"

using namespace eddy;

static ZoomSegment zoom(quint32 id, qint64 start, qint64 end) {
    ZoomSegment z;
    z.id = id;
    z.startMs = start;
    z.endMs = end;
    return z;
}

class TestZoomLane : public QObject {
    Q_OBJECT
private slots:
    void newZoomsTakeTwoSecondsInTheFreeGap() {
        const QVector<ZoomSegment> none;
        QCOMPARE(zoomlane::placeNew(none, 3000, 10000), (std::pair<qint64, qint64>{3000, 5000}));
        // Near the end the zoom moves back so it keeps its two seconds.
        QCOMPARE(zoomlane::placeNew(none, 9000, 10000), (std::pair<qint64, qint64>{8000, 10000}));
        const QVector<ZoomSegment> two{zoom(1, 1000, 3000), zoom(2, 3800, 6000)};
        // An 0.8 s gap takes an 0.8 s zoom.
        QCOMPARE(zoomlane::placeNew(two, 3100, 10000), (std::pair<qint64, qint64>{3000, 3800}));
        QVERIFY(!zoomlane::placeNew(two, 2000, 10000));        // inside a zoom
        const QVector<ZoomSegment> tight{zoom(1, 1000, 3000), zoom(2, 3400, 6000)};
        QVERIFY(!zoomlane::placeNew(tight, 3100, 10000));      // 0.4 s is too short
        QCOMPARE(zoomlane::placeNew(two, 0, 700), (std::pair<qint64, qint64>{0, 700}));
        QVERIFY(!zoomlane::placeNew(none, 0, 400));             // the whole clip is too short
    }

    void insertKeepsOrderAndIdsAreFresh() {
        QVector<ZoomSegment> zooms{zoom(4, 1000, 2000), zoom(9, 5000, 6000)};
        QCOMPARE(zoomlane::nextId(zooms), 10u);
        QCOMPARE(zoomlane::nextId({}), 1u);
        QCOMPARE(zoomlane::insert(zooms, zoom(10, 3000, 4000)), 1);
        QCOMPARE(zooms[1].id, 10u);
        QCOMPARE(zoomlane::insert(zooms, zoom(11, 0, 500)), 0);
        QCOMPARE(zoomlane::indexOf(zooms, 9), 3);
        QCOMPARE(zoomlane::indexOf(zooms, 99), -1);
    }

    void movingStopsAtNeighboursAndTheEnds() {
        QVector<ZoomSegment> zooms{zoom(1, 1000, 2000), zoom(2, 4000, 5000), zoom(3, 8000, 9000)};
        zoomlane::move(zooms, 2, 7500, 10000);
        QCOMPARE(zooms[1].startMs, 7000);    // against the next zoom
        QCOMPARE(zooms[1].endMs, 8000);
        zoomlane::move(zooms, 2, 500, 10000);
        QCOMPARE(zooms[1].startMs, 2000);    // against the previous one
        zoomlane::move(zooms, 1, -300, 10000);
        QCOMPARE(zooms[0].startMs, 0);
        zoomlane::move(zooms, 3, 9500, 10000);
        QCOMPARE(zooms[2].endMs, 10000);
    }

    void edgesKeepHalfASecondAndTheirNeighbours() {
        QVector<ZoomSegment> zooms{zoom(1, 1000, 2000), zoom(2, 4000, 5000)};
        zoomlane::resize(zooms, 2, false, 4200, 10000);
        QCOMPARE(zooms[1].endMs, 4500);      // at least 0.5 s
        zoomlane::resize(zooms, 2, true, 1500, 10000);
        QCOMPARE(zooms[1].startMs, 2000);    // not over the previous zoom
        zoomlane::resize(zooms, 1, false, 99999, 10000);
        QCOMPARE(zooms[0].endMs, 2000);      // not over the next one
        zoomlane::resize(zooms, 2, false, 99999, 10000);
        QCOMPARE(zooms[1].endMs, 10000);
    }

    void snapPicksTheNearestAnchorInReach() {
        QCOMPARE(zoomlane::snap(1050, {1000, 1100}, 60), 1000);
        QCOMPARE(zoomlane::snap(1070, {1000, 1100}, 60), 1100);
        QCOMPARE(zoomlane::snap(1500, {1000, 1100}, 60), 1500);
    }
};

QTEST_GUILESS_MAIN(TestZoomLane)
#include "test_zoomlane.moc"
```

- [x] **Step 2: Test laufen lassen, er scheitert** (Datei `zoomlane.h` fehlt).

- [x] **Step 3: Umsetzen.** `src/zoomlane.h`:

```cpp
#pragma once
#include <QVector>
#include <optional>
#include <utility>
#include "studiodocument.h"

// Editing zooms on their lane (studio plan 6.1): pure functions over the
// sorted, never overlapping list, so the timeline only paints and forwards.
namespace eddy::zoomlane {

inline constexpr qint64 kNewZoomMs = 2000;
inline constexpr qint64 kMinZoomMs = 500;

// Where a new zoom at `timeMs` goes: two seconds, moved back into the free
// gap around it, shorter only when the gap is; nothing inside a zoom or when
// the gap is under half a second.
std::optional<std::pair<qint64, qint64>> placeNew(const QVector<ZoomSegment> &zooms, qint64 timeMs,
                                                  qint64 durationMs);
// Inserts in start order and returns the index.
int insert(QVector<ZoomSegment> &zooms, const ZoomSegment &zoom);
quint32 nextId(const QVector<ZoomSegment> &zooms);
int indexOf(const QVector<ZoomSegment> &zooms, quint32 id);
// Body drag: keeps the length, stops at the neighbours and the clip's ends.
void move(QVector<ZoomSegment> &zooms, quint32 id, qint64 startMs, qint64 durationMs);
// Edge drag: at least kMinZoomMs long, never over a neighbour or past the ends.
void resize(QVector<ZoomSegment> &zooms, quint32 id, bool startEdge, qint64 timeMs, qint64 durationMs);
// The nearest anchor within `toleranceMs`, else `timeMs`.
qint64 snap(qint64 timeMs, const QVector<qint64> &anchors, qint64 toleranceMs);

}
```

`src/zoomlane.cpp`:

```cpp
#include "zoomlane.h"
#include <QtGlobal>
#include <algorithm>

namespace eddy::zoomlane {

std::optional<std::pair<qint64, qint64>> placeNew(const QVector<ZoomSegment> &zooms, qint64 timeMs,
                                                  qint64 durationMs) {
    qint64 lo = 0, hi = durationMs;
    for (const ZoomSegment &z : zooms) {
        if (z.startMs <= timeMs && timeMs < z.endMs) return std::nullopt;
        if (z.endMs <= timeMs) lo = std::max(lo, z.endMs);
        else hi = std::min(hi, z.startMs);
    }
    if (hi - lo < kMinZoomMs) return std::nullopt;
    const qint64 start = std::max(lo, std::min(timeMs, hi - kNewZoomMs));
    return std::pair{start, std::min(hi, start + kNewZoomMs)};
}

int insert(QVector<ZoomSegment> &zooms, const ZoomSegment &zoom) {
    const auto at = std::upper_bound(zooms.begin(), zooms.end(), zoom.startMs,
        [](qint64 start, const ZoomSegment &z) { return start < z.startMs; });
    const int index = int(at - zooms.begin());
    zooms.insert(index, zoom);
    return index;
}

quint32 nextId(const QVector<ZoomSegment> &zooms) {
    quint32 id = 0;
    for (const ZoomSegment &z : zooms) id = std::max(id, z.id);
    return id + 1;
}

int indexOf(const QVector<ZoomSegment> &zooms, quint32 id) {
    for (int i = 0; i < zooms.size(); ++i)
        if (zooms[i].id == id) return i;
    return -1;
}

void move(QVector<ZoomSegment> &zooms, quint32 id, qint64 startMs, qint64 durationMs) {
    const int i = indexOf(zooms, id);
    if (i < 0) return;
    const qint64 length = zooms[i].endMs - zooms[i].startMs;
    const qint64 lo = i > 0 ? zooms[i - 1].endMs : 0;
    const qint64 hi = i + 1 < zooms.size() ? zooms[i + 1].startMs : durationMs;
    zooms[i].startMs = qBound(lo, startMs, hi - length);
    zooms[i].endMs = zooms[i].startMs + length;
}

void resize(QVector<ZoomSegment> &zooms, quint32 id, bool startEdge, qint64 timeMs, qint64 durationMs) {
    const int i = indexOf(zooms, id);
    if (i < 0) return;
    ZoomSegment &z = zooms[i];
    if (startEdge)
        z.startMs = qBound(i > 0 ? zooms[i - 1].endMs : 0, timeMs, z.endMs - kMinZoomMs);
    else
        z.endMs = qBound(z.startMs + kMinZoomMs, timeMs,
                         i + 1 < zooms.size() ? zooms[i + 1].startMs : durationMs);
}

qint64 snap(qint64 timeMs, const QVector<qint64> &anchors, qint64 toleranceMs) {
    qint64 best = timeMs, distance = toleranceMs + 1;
    for (qint64 anchor : anchors) {
        const qint64 d = qAbs(anchor - timeMs);
        if (d <= toleranceMs && d < distance) {
            best = anchor;
            distance = d;
        }
    }
    return best;
}

}
```

- [x] **Step 4: Build und Test.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R zoomlane --output-on-failure` · Expected: bestanden.

---

### Task 3: Zoom-Spur in der Timeline

**Files:**
- Modify: `src/videotimeline.h`, `src/videotimeline.cpp`
- Test: `tests/test_videotimeline.cpp`

**Interfaces:**
- Consumes: `zoomlane::*` (Task 2).
- Produces (`VideoTimeline`): `void setZoomLaneVisible(bool)`, `bool zoomLaneVisible() const`,
  `void setZooms(const QVector<ZoomSegment> &, std::function<double(qint64 sourceMs)> level = {})`,
  `QVector<ZoomSegment> zooms() const`, `void setSelectedZoom(quint32)`, `quint32 selectedZoom() const`,
  `QRectF zoomLaneRect() const`, `bool zoomDragging() const`;
  Signale `zoomAddRequested(qint64 timeMs)`, `zoomSelected(quint32 id)`,
  `zoomsPreviewed(const QVector<ZoomSegment> &)`,
  `zoomsEdited(const QVector<ZoomSegment> &before, const QVector<ZoomSegment> &after)`,
  `zoomMenuRequested(quint32 id, QPoint globalPos)`.
  Höhe: 52 px ohne Spur, 84 px mit Spur (Filmstreifen 30 px, 4 px Fuge, Spur 28 px, 4 px Rand).

- [x] **Step 1: Tests schreiben** (in `TestVideoTimeline`, Include `#include <QSignalSpy>`):

```cpp
    void zoomLaneGrowsTheTimelineUnderTheFilmStrip() {
        VideoTimeline timeline;
        QCOMPARE(timeline.height(), 52);
        QVERIFY(timeline.zoomLaneRect().isEmpty());
        timeline.setZoomLaneVisible(true);
        QCOMPARE(timeline.height(), 84);
        QCOMPARE(timeline.zoomLaneRect().top(), 52.0);
        QCOMPARE(timeline.zoomLaneRect().height(), 28.0);
        timeline.setZoomLaneVisible(false);
        QCOMPARE(timeline.height(), 52);
    }
    void clickingTheEmptyLaneAsksForAZoomThere() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        timeline.show();
        QSignalSpy add(&timeline, &VideoTimeline::zoomAddRequested);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QCOMPARE(add.count(), 1);
        QCOMPARE(add.first().first().toLongLong(), 5000);
    }
    void draggingAZoomMovesItAsOneEdit() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 1; z.startMs = 2000; z.endMs = 4000;
        timeline.setZooms({z});
        timeline.show();
        QSignalSpy selected(&timeline, &VideoTimeline::zoomSelected);
        QSignalSpy edited(&timeline, &VideoTimeline::zoomsEdited);
        // A plain click selects without moving, even next to an anchor.
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(96, 66));
        QCOMPARE(selected.count(), 1);
        QCOMPARE(timeline.selectedZoom(), 1u);
        QCOMPARE(edited.count(), 0);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(96, 66));
        QTest::mouseMove(&timeline, QPoint(126, 66));
        QTest::mouseMove(&timeline, QPoint(156, 66));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QCOMPARE(edited.count(), 1);
        const auto after = edited.first().at(1).value<QVector<ZoomSegment>>();
        QCOMPARE(after.first().startMs, 4000);
        QCOMPARE(after.first().endMs, 6000);
        QCOMPARE(timeline.zooms().first().startMs, 4000);
    }
    void edgesSnapToThePlayheadAndEscapeRestores() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setPosition(7000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 3; z.startMs = 2000; z.endMs = 4000;
        timeline.setZooms({z});
        timeline.show();
        QSignalSpy edited(&timeline, &VideoTimeline::zoomsEdited);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(126, 66));   // end edge
        QTest::mouseMove(&timeline, QPoint(170, 66));
        QTest::mouseMove(&timeline, QPoint(218, 66));   // 7066 ms, 6 px snap reach = 200 ms
        QCOMPARE(timeline.zooms().first().endMs, 7000);
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QCOMPARE(timeline.zooms().first().endMs, 4000);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(218, 66));
        QCOMPARE(edited.count(), 0);
    }
    void rightClickOnAZoomAsksForItsMenu() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 5; z.startMs = 2000; z.endMs = 4000;
        timeline.setZooms({z});
        QSignalSpy menu(&timeline, &VideoTimeline::zoomMenuRequested);
        emit timeline.customContextMenuRequested(QPoint(96, 66));
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.first().first().toUInt(), 5u);
        QCOMPARE(timeline.selectedZoom(), 5u);
    }
    void laneShowsTheCameraCurve() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 1; z.startMs = 2000; z.endMs = 8000; z.scale = 2;
        timeline.setZooms({z}, [](qint64 t) { return t >= 3000 && t < 8000 ? 2.0 : 1.0; });
        const QImage shot = timeline.grab().toImage();
        // Zoomed in, the curve fills the block near its top; before the ramp it does not.
        QVERIFY(qGray(shot.pixel(156, 58)) != qGray(shot.pixel(72, 58)));
    }
```

`Q_DECLARE_METATYPE` ist für `QVector<ZoomSegment>` nötig: in `src/studiodocument.h` am Ende
außerhalb des Namespace `Q_DECLARE_METATYPE(eddy::ZoomSegment)` (Qt 6 registriert
`QVector<T>` dann selbst).

- [x] **Step 2: Tests laufen lassen, sie scheitern am Build.**

- [x] **Step 3: Umsetzen.**

`src/videotimeline.h`: Includes `<functional>`, `"studiodocument.h"`. Öffentlich:

```cpp
    // Zoom lane (studio plan 6.1, Q1 = C): a 28 px row under the film strip on
    // the same time axis, shown while Studio is on or zooms exist.
    void setZoomLaneVisible(bool visible);
    bool zoomLaneVisible() const { return m_laneVisible; }
    // `level` is the camera's zoom at a source time, 1 when it shows everything.
    void setZooms(const QVector<ZoomSegment> &zooms, std::function<double(qint64)> level = {});
    QVector<ZoomSegment> zooms() const { return m_zooms; }
    void setSelectedZoom(quint32 id);   // 0: none
    quint32 selectedZoom() const { return m_selectedZoom; }
    QRectF zoomLaneRect() const;
    bool zoomDragging() const {
        return m_drag == Drag::ZoomMove || m_drag == Drag::ZoomStart || m_drag == Drag::ZoomEnd;
    }
```

Signale ergänzen:

```cpp
    void zoomAddRequested(qint64 timeMs);
    void zoomSelected(quint32 id);
    void zoomsPreviewed(const QVector<ZoomSegment> &zooms);
    void zoomsEdited(const QVector<ZoomSegment> &before, const QVector<ZoomSegment> &after);
    void zoomMenuRequested(quint32 id, QPoint globalPos);
```

Privat: `enum class Drag { None, In, Out, Seek, ZoomMove, ZoomStart, ZoomEnd };` (die
Deklaration wandert vor die öffentlichen Methoden, weil `zoomDragging()` sie braucht) und

```cpp
    quint32 zoomAt(QPointF pos, Drag *part) const;
    void moveZoomDrag(qreal x);
    bool m_laneVisible = false;
    QVector<ZoomSegment> m_zooms, m_zoomsBefore;
    std::function<double(qint64)> m_zoomLevel;
    quint32 m_selectedZoom = 0, m_dragZoom = 0;
    qint64 m_grabOffset = 0;
    bool m_zoomMoved = false;
```

`src/videotimeline.cpp`: Includes `"zoomlane.h"`, `<QFontDatabase>`.

Kontextmenü im Konstruktor ersetzen:

```cpp
    connect(this, &QWidget::customContextMenuRequested, this, [this](QPoint pos) {
        emit hoverLeft();
        Drag part = Drag::None;
        if (const quint32 id = zoomAt(pos, &part)) {
            setSelectedZoom(id);
            emit zoomSelected(id);
            emit zoomMenuRequested(id, mapToGlobal(pos));
            return;
        }
        QMenu menu(this);
        if (m_laneVisible && pos.y() >= zoomLaneRect().top() - 2) {
            const qint64 time = timeForX(pos.x());
            menu.addAction(tr("Add zoom here"), this, [this, time] { emit zoomAddRequested(time); });
            menu.addSeparator();
        }
        menu.addAction(tr("Zoom in · +"), this, [this] { zoomAt(2, m_position); });
        menu.addAction(tr("Zoom out · −"), this, [this] { zoomAt(0.5, m_position); });
        menu.addAction(tr("Fit clip · 0"), this, &VideoTimeline::fitClip);
        menu.exec(mapToGlobal(pos));
    });
```

(Die bisherige `zoomAt(qreal, qint64)` für das Timeline-Zoomen bleibt; die neue private
`zoomAt(QPointF, Drag *)` ist eine Überladung.)

Neue Methoden:

```cpp
void VideoTimeline::setZoomLaneVisible(bool visible) {
    if (visible == m_laneVisible) return;
    cancelInteraction();
    m_laneVisible = visible;
    setFixedHeight(visible ? 84 : 52);
    update();
}

void VideoTimeline::setZooms(const QVector<ZoomSegment> &zooms, std::function<double(qint64)> level) {
    if (zoomDragging()) return;   // the drag owns the list until it ends
    m_zooms = zooms;
    m_zoomLevel = std::move(level);
    if (zoomlane::indexOf(m_zooms, m_selectedZoom) < 0) m_selectedZoom = 0;
    update();
}

void VideoTimeline::setSelectedZoom(quint32 id) {
    if (zoomlane::indexOf(m_zooms, id) < 0) id = 0;
    if (id == m_selectedZoom) return;
    m_selectedZoom = id;
    update();
}

QRectF VideoTimeline::zoomLaneRect() const {
    if (!m_laneVisible) return {};
    const QRectF track = trackRect();
    return QRectF(track.left(), track.bottom() + 4, track.width(), 28);
}

quint32 VideoTimeline::zoomAt(QPointF pos, Drag *part) const {
    *part = Drag::None;
    const QRectF lane = zoomLaneRect();
    if (lane.isEmpty() || pos.y() < lane.top() - 2 || pos.y() > lane.bottom() + 2) return 0;
    for (const ZoomSegment &z : m_zooms) {
        const qreal x0 = xForTime(z.startMs), x1 = xForTime(z.endMs);
        const qreal d0 = qAbs(pos.x() - x0), d1 = qAbs(pos.x() - x1);
        if (qMin(d0, d1) <= 6) *part = d0 <= d1 ? Drag::ZoomStart : Drag::ZoomEnd;
        else if (pos.x() > x0 && pos.x() < x1) *part = Drag::ZoomMove;
        else continue;
        return z.id;
    }
    return 0;
}

void VideoTimeline::moveZoomDrag(qreal x) {
    // A click without movement only selects, even beside an anchor.
    if (!m_zoomMoved && qAbs(x - m_dragX) < 3) return;
    m_zoomMoved = true;
    const int index = zoomlane::indexOf(m_zooms, m_dragZoom);
    if (index < 0) return;
    const qint64 reach = qRound64(6.0 * (m_viewEnd - m_viewStart) / trackRect().width());
    QVector<qint64> anchors{m_position, m_in, m_out};
    for (const ZoomSegment &z : std::as_const(m_zooms))
        if (z.id != m_dragZoom) anchors << z.startMs << z.endMs;
    const qint64 time = timeForX(x) - m_grabOffset;
    if (m_drag == Drag::ZoomMove) {
        const qint64 length = m_zooms[index].endMs - m_zooms[index].startMs;
        const qint64 byStart = zoomlane::snap(time, anchors, reach);
        const qint64 byEnd = zoomlane::snap(time + length, anchors, reach) - length;
        qint64 start = byStart;
        if (byEnd != time && (byStart == time || qAbs(byEnd - time) < qAbs(byStart - time))) start = byEnd;
        zoomlane::move(m_zooms, m_dragZoom, start, m_duration);
    } else {
        zoomlane::resize(m_zooms, m_dragZoom, m_drag == Drag::ZoomStart,
                         zoomlane::snap(time, anchors, reach), m_duration);
    }
    emit zoomsPreviewed(m_zooms);
    update();
}
```

`trackRect()`:

```cpp
QRectF VideoTimeline::trackRect() const {
    return QRectF(6, 18, qMax(1, width() - 12), height() - 22 - (m_laneVisible ? 32 : 0));
}
```

`paintEvent`: direkt nach dem `painter.restore();` des Filmstreifens:

```cpp
    if (m_laneVisible) {
        const QRectF lane = zoomLaneRect();
        QPainterPath laneShape;
        laneShape.addRoundedRect(lane, 6, 6);
        painter.save();
        painter.setClipPath(laneShape);
        painter.fillRect(lane, ink(0.07));
        double top = 2;
        for (const ZoomSegment &z : std::as_const(m_zooms)) top = qMax(top, z.scale);
        for (const ZoomSegment &z : std::as_const(m_zooms)) {
            const qreal x0 = xForTime(z.startMs), x1 = xForTime(z.endMs);
            if (x1 < lane.left() || x0 > lane.right()) continue;
            painter.fillRect(QRectF(x0, lane.top(), qMax<qreal>(2, x1 - x0), lane.height()),
                             ink(z.id == m_selectedZoom ? 0.2 : 0.13));
        }
        if (m_zoomLevel && !m_zooms.isEmpty()) {
            // The camera's actual zoom over time, ramps and all (Q1 = C).
            QPainterPath area;
            area.moveTo(lane.left(), lane.bottom());
            for (qreal x = lane.left();; x += 2) {
                const qreal at = qMin(x, lane.right());
                const double level = m_zoomLevel(timeForX(at));
                area.lineTo(at, lane.bottom() - (lane.height() - 4) * qBound(0.0, (level - 1) / (top - 1), 1.0));
                if (at >= lane.right()) break;
            }
            area.lineTo(lane.right(), lane.bottom());
            area.closeSubpath();
            QColor curve = foreground;
            curve.setAlphaF(0.16);
            painter.fillPath(area, curve);
        }
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(theme::kFsMicro);
        QFont label = font();
        label.setPixelSize(theme::kFsMicro);
        for (const ZoomSegment &z : std::as_const(m_zooms)) {
            const qreal x0 = xForTime(z.startMs), x1 = xForTime(z.endMs);
            if (x1 < lane.left() || x0 > lane.right()) continue;
            const bool selected = z.id == m_selectedZoom;
            // Numbers in mono, words in the UI face (studio plan 6.1).
            const QString scale = QString::number(z.scale, 'g', 3) + QStringLiteral("×");
            const QString motion = z.motion == ZoomSegment::Motion::Focused ? tr("Focused")
                : z.motion == ZoomSegment::Motion::Smooth ? tr("Smooth") : tr("Instant");
            const qreal scaleWidth = QFontMetricsF(mono).horizontalAdvance(scale);
            const qreal motionWidth = QFontMetricsF(label).horizontalAdvance(QStringLiteral(" · ") + motion);
            const qreal left = qMax(x0, lane.left()) + 8;
            const qreal room = qMin(x1, lane.right()) - 8 - left;
            painter.setPen(ink(selected ? 0.9 : 0.7));
            if (room >= scaleWidth) {
                painter.setFont(mono);
                painter.drawText(QRectF(left, lane.top(), scaleWidth, lane.height()), Qt::AlignVCenter, scale);
            }
            if (room >= scaleWidth + motionWidth) {
                painter.setFont(label);
                painter.drawText(QRectF(left + scaleWidth, lane.top(), motionWidth, lane.height()),
                                 Qt::AlignVCenter, QStringLiteral(" · ") + motion);
            }
            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink(0.7));
                for (qreal x : {x0 + 3, x1 - 5})
                    painter.drawRoundedRect(QRectF(x, lane.center().y() - 6, 2, 12), 1, 1);
            }
        }
        if (m_zooms.isEmpty()) {
            painter.setFont(label);
            painter.setPen(ink(0.45));
            painter.drawText(lane, Qt::AlignCenter, tr("Click to add a zoom · Z"));
        }
        painter.restore();
    }
```

Der Playhead reicht mit Spur bis zu ihrem Ende; statt
`painter.drawRoundedRect(QRectF(playX - 1, track.top(), 2, track.height() + 3), 1, 1);`:

```cpp
        const qreal playBottom = m_laneVisible ? zoomLaneRect().bottom() : track.bottom() + 3;
        painter.drawRoundedRect(QRectF(playX - 1, track.top(), 2, playBottom - track.top()), 1, 1);
```

`mousePressEvent` nach `setFocus(Qt::MouseFocusReason);`:

```cpp
    if (m_laneVisible && event->position().y() >= zoomLaneRect().top() - 2) {
        Drag part = Drag::None;
        const quint32 id = zoomAt(event->position(), &part);
        if (!id) {
            emit zoomAddRequested(timeForX(event->position().x()));
            event->accept();
            return;
        }
        setSelectedZoom(id);
        emit zoomSelected(id);
        const ZoomSegment &z = m_zooms[zoomlane::indexOf(m_zooms, id)];
        m_drag = part;
        m_dragZoom = id;
        m_zoomsBefore = m_zooms;
        m_zoomMoved = false;
        m_dragX = event->position().x();
        m_grabOffset = timeForX(m_dragX) - (part == Drag::ZoomEnd ? z.endMs : z.startMs);
        m_edgePan.start();
        event->accept();
        return;
    }
```

`mouseMoveEvent`, Zweig `m_drag == Drag::None` ganz vorne:

```cpp
        if (m_laneVisible && event->position().y() >= zoomLaneRect().top() - 2) {
            Drag part = Drag::None;
            zoomAt(event->position(), &part);
            setCursor(part == Drag::ZoomStart || part == Drag::ZoomEnd ? Qt::SizeHorCursor
                                                                     : Qt::PointingHandCursor);
            if (m_hover != Drag::None) { m_hover = Drag::None; update(); }
            emit hoverLeft();
            return;
        }
```

und im Filmstreifen-Zweig den Cursor bei jeder Bewegung setzen (nicht nur bei
Hover-Wechsel), damit er nach der Spur zurückspringt:

```cpp
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
        setCursor(hover == Drag::None ? Qt::PointingHandCursor : Qt::SizeHorCursor);
```

`moveDrag` nach `m_modifiers = modifiers;`:

```cpp
    if (zoomDragging()) { moveZoomDrag(x); return; }
```

`mouseReleaseEvent` nach der ersten Prüfung:

```cpp
    if (zoomDragging()) {
        moveZoomDrag(event->position().x());
        m_drag = Drag::None;
        m_edgePan.stop();
        if (m_zooms != m_zoomsBefore) emit zoomsEdited(m_zoomsBefore, m_zooms);
        update();
        event->accept();
        return;
    }
```

`cancelInteraction` nach `if (!interacting()) return;`:

```cpp
    if (zoomDragging()) {
        m_drag = Drag::None;
        m_edgePan.stop();
        m_zooms = m_zoomsBefore;
        emit zoomsPreviewed(m_zooms);
        update();
        return;
    }
```

- [x] **Step 4: Build und Tests.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R videotimeline --output-on-failure` · Expected: alle bestanden (auch die alten; ohne Spur ändert sich nichts).

---

### Task 4: Kamera im Canvas

**Files:**
- Modify: `src/canvas.h`, `src/canvas.cpp`
- Test: `tests/test_canvas.cpp`

**Interfaces:**
- Produces (`Canvas`): `void setCamera(const QRectF &camera)` (Dokumentpixel; leer oder gleich
  `contentRect()` = keine Kamera), `QRectF camera() const`, `void setCameraDragEnabled(bool)`,
  `bool cameraDragging() const`, `void cancelCameraDrag()`; Signale
  `cameraDragged(QPointF documentDelta)` (Zeigerbewegung in Dokumentpixeln; der Inhalt folgt dem
  Zeiger, die Kamera bewegt sich also um `-documentDelta`), `cameraDragFinished(bool cancelled)`.

- [x] **Step 1: Tests schreiben** (in `TestCanvas`):

```cpp
    void cameraShowsItsWindowWhereTheContentSits() {
        QGraphicsScene scene(0, 0, 400, 200);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(400, 200, QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        canvas.setAnimationsEnabled(false);
        canvas.resize(440, 240);
        canvas.show();
        canvas.resetZoom();
        const QPoint topLeft = canvas.mapFromScene(QPointF(0, 0));
        const QPoint bottomRight = canvas.mapFromScene(QPointF(400, 200));
        auto near = [](QPoint a, QPoint b) { return (a - b).manhattanLength() <= 2; };
        // 2x on the top-right quarter: it fills the place of the whole content.
        canvas.setCamera(QRectF(200, 0, 200, 100));
        QCOMPARE(canvas.camera(), QRectF(200, 0, 200, 100));
        QVERIFY(near(canvas.mapFromScene(QPointF(200, 0)), topLeft));
        QVERIFY(near(canvas.mapFromScene(QPointF(400, 100)), bottomRight));
        // The user's own zoom stays theirs.
        canvas.zoomBy(2);
        QCOMPARE(canvas.zoom(), 2.0);
        canvas.setCamera({});
        QVERIFY(canvas.camera().isEmpty());
        QCOMPARE(canvas.transform().m11(), 2.0);
        canvas.resetZoom();
        QVERIFY(near(canvas.mapFromScene(QPointF(0, 0)), topLeft));
        // A camera equal to the content is no camera.
        canvas.setCamera(QRectF(0, 0, 400, 200));
        QVERIFY(canvas.camera().isEmpty());
    }
    void studioFrameStaysPutUnderTheCamera() {
        QGraphicsScene scene(0, 0, 400, 200);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(400, 200, QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        canvas.setAnimationsEnabled(false);
        canvas.resize(520, 320);
        canvas.show();
        QPixmap red(10, 10);
        red.fill(Qt::red);
        canvas.setStudioFrame(red, QRectF(-40, -40, 480, 280), 0);
        canvas.resetZoom();
        const QPoint frame = canvas.mapFromScene(QPointF(-20, 100));
        QCOMPARE(canvas.viewport()->grab().toImage().pixelColor(frame), QColor(Qt::red));
        canvas.setCamera(QRectF(100, 50, 200, 100));
        QCOMPARE(canvas.viewport()->grab().toImage().pixelColor(frame), QColor(Qt::red));
    }
    void cameraDragReportsDocumentDistances() {
        QGraphicsScene scene(0, 0, 400, 200);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(400, 200, QImage::Format_ARGB32_Premultiplied));
        auto *item = new RectItem(QRectF(0, 0, 40, 40));
        item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        item->setPos(20, 20);
        scene.addItem(item);
        tools.setTool(ToolType::Move);   // the default tool is Arrow
        Canvas canvas(&scene, &tools);
        canvas.setAnimationsEnabled(false);
        canvas.resize(440, 240);
        canvas.show();
        canvas.resetZoom();
        canvas.setCamera(QRectF(0, 0, 200, 100));   // 2x
        canvas.setCameraDragEnabled(true);
        QSignalSpy dragged(&canvas, &Canvas::cameraDragged);
        QSignalSpy finished(&canvas, &Canvas::cameraDragFinished);
        const QPoint empty = canvas.mapFromScene(QPointF(150, 70));
        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, empty);
        QTest::mouseMove(canvas.viewport(), empty + QPoint(40, 0));
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, empty + QPoint(40, 0));
        QPointF total;
        for (const auto &args : dragged) total += args.first().toPointF();
        QVERIFY(qAbs(total.x() - 20) < 0.5 && qAbs(total.y()) < 0.5);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toBool(), false);
        // On an annotation the drag moves the annotation, as always.
        dragged.clear();
        const QPoint onItem = canvas.mapFromScene(QPointF(30, 30));
        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, onItem);
        QTest::mouseMove(canvas.viewport(), onItem + QPoint(20, 0));
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, onItem + QPoint(20, 0));
        QCOMPARE(dragged.count(), 0);
        QVERIFY(item->pos().x() > 20);
    }
```

- [x] **Step 2: Tests laufen lassen, sie scheitern am Build.**

- [x] **Step 3: Umsetzen.**

`src/canvas.h`, öffentlich:

```cpp
    // Studio zoom preview (studio plan 5): the `camera` window (document
    // pixels) fills the place of the content rect. The Studio frame and the
    // user's own zoom and pan stay where they are.
    void setCamera(const QRectF &camera);
    QRectF camera() const { return m_cameraRect; }
    // Move-tool drags on empty content steer the camera instead of selecting.
    void setCameraDragEnabled(bool on) { m_cameraDrag = on; }
    bool cameraDragging() const { return m_cameraDragging; }
    void cancelCameraDrag();
```

Signale:

```cpp
    void cameraDragged(QPointF documentDelta);   // the pointer moved this far over the document
    void cameraDragFinished(bool cancelled);
```

Privat:

```cpp
    template <typename Change> void withoutCamera(Change change);
    void applyCamera();
    QTransform viewWithoutCamera() const;
    QPointF viewportCentreInScene() const;
    QTransform m_camera;         // scene -> scene: the camera window onto the content rect
    QRectF m_cameraRect;
    double m_viewZoom = 1.0;     // the user's view while a camera is on
    QPointF m_viewCentre;
    bool m_cameraDrag = false, m_cameraDragging = false;
    QPointF m_cameraDragLast;
```

`src/canvas.cpp`:

```cpp
QPointF Canvas::viewportCentreInScene() const {
    return viewportTransform().inverted().map(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
}

// Every zoom, pan and fit works on the user's own view as if there were no
// camera; the camera goes back on afterwards.
template <typename Change> void Canvas::withoutCamera(Change change) {
    if (m_camera.isIdentity()) { change(); return; }
    const QTransform camera = m_camera;
    m_camera = QTransform();
    setTransform(QTransform::fromScale(m_viewZoom, m_viewZoom));
    updateNavigationBounds();
    centerOn(m_viewCentre);
    change();
    m_viewZoom = transform().m11();
    m_viewCentre = viewportCentreInScene();
    m_camera = camera;
    applyCamera();
}

void Canvas::applyCamera() {
    const qreal k = m_camera.m11();
    setTransform(QTransform::fromScale(m_viewZoom * k, m_viewZoom * k));
    updateNavigationBounds();
    centerOn(m_camera.inverted().map(m_viewCentre));
}

QTransform Canvas::viewWithoutCamera() const {
    if (m_camera.isIdentity()) return viewportTransform();
    return QTransform::fromTranslate(-m_viewCentre.x(), -m_viewCentre.y())
         * QTransform::fromScale(m_viewZoom, m_viewZoom)
         * QTransform::fromTranslate(viewport()->width() / 2.0, viewport()->height() / 2.0);
}

void Canvas::setCamera(const QRectF &camera) {
    const QRectF base = contentRect();
    QTransform next;
    if (!camera.isEmpty() && !base.isEmpty() && camera != base) {
        const qreal k = base.width() / camera.width();
        next = QTransform::fromTranslate(-camera.left(), -camera.top()) * QTransform::fromScale(k, k)
             * QTransform::fromTranslate(base.left(), base.top());
    }
    m_cameraRect = next.isIdentity() ? QRectF() : camera;
    if (next == m_camera) return;
    if (m_camera.isIdentity()) {
        m_viewZoom = transform().m11();
        m_viewCentre = viewportCentreInScene();
    }
    m_camera = next;
    applyCamera();
    viewport()->update();
}

void Canvas::cancelCameraDrag() {
    if (!m_cameraDragging) return;
    m_cameraDragging = false;
    updateCursor();
    emit cameraDragFinished(true);
}
```

Nutzeraktionen laufen durch `withoutCamera`:

- `zoomBy`, Zweig ohne Animation: `withoutCamera([&] { scale(inc, inc); updateNavigationBounds(); });`
  statt der beiden Zeilen; im `valueChanged`-Lambda ebenso
  `withoutCamera([&] { scale(inc, inc); updateNavigationBounds(); });`.
- `resetZoom`, `fitMedia`, `restoreView`: den ganzen Körper (ohne das abschließende
  `emit viewChanged();`) in `withoutCamera([&] { … });` fassen.
- `mouseMoveEvent`, Mittelklick-Schwenken: die beiden `setValue`-Zeilen in
  `withoutCamera([&] { … });`.
- `resizeEvent`: `withoutCamera([&] { if (m_fitted) fitMedia(); else updateNavigationBounds(); });`
  (`fitMedia` ruft selbst `withoutCamera`; verschachtelt ist das die Identität, weil die
  innere Kamera dann schon aus ist.)
- `setContentRect`: am Ende `if (!m_cameraRect.isEmpty()) { const QRectF c = m_cameraRect; m_camera = QTransform(); m_cameraRect = {}; setCamera(c); }`
  vor dem `viewport()->update()`; die Basis hat sich geändert.

Hinweis zu `withoutCamera` innerhalb von `withoutCamera`: Beim inneren Aufruf ist `m_camera`
schon die Identität, also läuft `change()` direkt.

`drawForeground`, Studio-Zweig: `const QTransform view = viewportTransform();` wird
`const QTransform view = viewWithoutCamera();`.

`mousePressEvent`, direkt vor `QGraphicsView::mousePressEvent(e); // Move/Text: native selection/edit`:

```cpp
    if (e->button() == Qt::LeftButton && m_cameraDrag && m_tools->tool() == ToolType::Move
        && e->modifiers() == Qt::NoModifier) {
        const QGraphicsItem *item = itemAt(e->pos());
        if (!item || item->zValue() <= -1000) {
            scene()->clearSelection();
            m_cameraDragging = true;
            m_cameraDragLast = e->position();
            viewport()->setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
    }
```

`mouseMoveEvent` nach dem Pipetten-Zweig:

```cpp
    if (m_cameraDragging) {
        const QPointF delta = (e->position() - m_cameraDragLast) / transform().m11();
        m_cameraDragLast = e->position();
        emit cameraDragged(delta);
        e->accept();
        return;
    }
```

`mouseReleaseEvent` nach dem `m_swallowRelease`-Zweig:

```cpp
    if (m_cameraDragging && e->button() == Qt::LeftButton) {
        m_cameraDragging = false;
        updateCursor();
        emit cameraDragFinished(false);
        e->accept();
        return;
    }
```

- [x] **Step 4: Build und Tests.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R "canvas|crop" --output-on-failure` · Expected: alle bestanden.

---

### Task 5: Motion-Symbole und Kontextleiste

**Files:**
- Create: `src/motionicon.h`, `src/motionicon.cpp`, `src/zoombar.h`, `src/zoombar.cpp`, `tests/test_zoombar.cpp`
- Modify: `CMakeLists.txt` (beide Quellen nach `src/cropbar.cpp`, `eddy_test(test_zoombar)` nach `eddy_test(test_redactbar)`),
  `resources/eddy.qss`

**Interfaces:**
- Consumes: `cameraOmega`, `springStep` (`camerapath.h`).
- Produces: `QPolygonF motionCurve(ZoomSegment::Motion)` (im 24er-Raster),
  `QIcon motionIcon(ZoomSegment::Motion, const QColor &ink, int size)`,
  `QString motionName(ZoomSegment::Motion)`;
  `class ZoomBar : QWidget { void setZoom(const ZoomSegment &); void refreshTheme(); signals: scaleChosen(double), motionChosen(ZoomSegment::Motion), removeRequested(); }`.
  Objektnamen: `ZoomBar`, `ZoomScale`, `ZoomMotion`, `ZoomRemove`, Menüs `ZoomScaleMenu`, `ZoomMotionMenu`.

- [x] **Step 1: Test schreiben** `tests/test_zoombar.cpp`:

```cpp
#include <QtTest>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <cmath>
#include "motionicon.h"
#include "zoombar.h"

using namespace eddy;

// The height the curve reaches at `seconds` after the step, 0..1.
static double curveAt(const QPolygonF &curve, double seconds) {
    const double x = 3.4 + (0.25 + seconds) / 1.45 * 17.2;
    QPointF best = curve.first();
    for (const QPointF &p : curve)
        if (qAbs(p.x() - x) < qAbs(best.x() - x)) best = p;
    return (20.6 - best.y()) / 17.2;
}

class TestZoomBar : public QObject {
    Q_OBJECT
private slots:
    void motionCurvesAreTheCamerasSpring() {
        // Critically damped step response: 1 - (1 + w t) e^(-w t).
        auto step = [](double w, double t) { return 1 - (1 + w * t) * std::exp(-w * t); };
        const QPolygonF focused = motionCurve(ZoomSegment::Motion::Focused);
        const QPolygonF smooth = motionCurve(ZoomSegment::Motion::Smooth);
        for (double t : {0.1, 0.3, 0.6}) {
            QVERIFY(qAbs(curveAt(focused, t) - step(10, t)) < 0.04);
            QVERIFY(qAbs(curveAt(smooth, t) - step(6, t)) < 0.04);
        }
        QCOMPARE(curveAt(focused, -0.1), 0.0);
        const QPolygonF instant = motionCurve(ZoomSegment::Motion::Instant);
        QCOMPARE(curveAt(instant, 0.8), 1.0);
    }
    void motionIconsShareTheIconGrid() {
        for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                            ZoomSegment::Motion::Instant}) {
            const QImage sheet = motionIcon(motion, Qt::black, 240).pixmap(240, 240).toImage();
            int left = 240, top = 240, right = -1, bottom = -1;
            for (int y = 0; y < sheet.height(); ++y)
                for (int x = 0; x < sheet.width(); ++x)
                    if (qAlpha(sheet.pixel(x, y))) {
                        left = qMin(left, x); right = qMax(right, x);
                        top = qMin(top, y); bottom = qMax(bottom, y);
                    }
            const double unit = sheet.width() / 24.0;
            const double width = (right - left + 1) / unit, height = (bottom - top + 1) / unit;
            QVERIFY2(qAbs((left + right + 1) / 2.0 / unit - 12) <= 0.2, qPrintable(motionName(motion)));
            QVERIFY2(qAbs((top + bottom + 1) / 2.0 / unit - 12) <= 0.3, qPrintable(motionName(motion)));
            QVERIFY2(qAbs(qMax(width, height) - 20) <= 0.3,
                     qPrintable(QStringLiteral("%1 live %2").arg(motionName(motion)).arg(qMax(width, height))));
        }
    }
    void barShowsTheZoomAndReportsChoices() {
        ZoomBar bar;
        ZoomSegment z;
        z.scale = 2;
        z.motion = ZoomSegment::Motion::Smooth;
        bar.setZoom(z);
        auto *scale = bar.findChild<QToolButton *>(QStringLiteral("ZoomScale"));
        auto *motion = bar.findChild<QToolButton *>(QStringLiteral("ZoomMotion"));
        QVERIFY(scale && motion);
        QCOMPARE(scale->text(), QStringLiteral("2×"));
        QCOMPARE(motion->text(), QStringLiteral("Smooth"));
        z.scale = 2.4;
        bar.setZoom(z);
        QCOMPARE(scale->text(), QStringLiteral("2.4×"));
        QSignalSpy scales(&bar, &ZoomBar::scaleChosen);
        QSignalSpy motions(&bar, &ZoomBar::motionChosen);
        QSignalSpy removes(&bar, &ZoomBar::removeRequested);
        bar.findChild<QMenu *>(QStringLiteral("ZoomScaleMenu"))->actions().at(3)->trigger();
        QCOMPARE(scales.first().first().toDouble(), 3.0);
        bar.findChild<QMenu *>(QStringLiteral("ZoomMotionMenu"))->actions().at(2)->trigger();
        QCOMPARE(motions.first().first().value<ZoomSegment::Motion>(), ZoomSegment::Motion::Instant);
        bar.findChild<QToolButton *>(QStringLiteral("ZoomRemove"))->click();
        QCOMPARE(removes.count(), 1);
    }
};

QTEST_MAIN(TestZoomBar)
#include "test_zoombar.moc"
```

(`Q_DECLARE_METATYPE(eddy::ZoomSegment::Motion)` gehört zu den Metatypen am Ende von
`studiodocument.h`, neben `eddy::ZoomSegment` aus Task 3.)

- [x] **Step 2: Test laufen lassen, er scheitert am Build.**

- [x] **Step 3: Umsetzen.** `src/motionicon.h`:

```cpp
#pragma once
#include <QColor>
#include <QIcon>
#include <QPolygonF>
#include "studiodocument.h"

namespace eddy {

// The Motion symbols (studio plan 11, Q4b and Q4d): the step response the
// camera really uses, so the symbols change with the springs. 0.25 s before
// the step and 1.2 s after it, in the icon set's 24-unit box with a 20-unit
// live area and its 2.8 stroke.
QPolygonF motionCurve(ZoomSegment::Motion motion);
QIcon motionIcon(ZoomSegment::Motion motion, const QColor &ink, int size);
QString motionName(ZoomSegment::Motion motion);

}
```

`src/motionicon.cpp`:

```cpp
#include "motionicon.h"
#include "camerapath.h"
#include <QCoreApplication>
#include <QIconEngine>
#include <QPainter>

namespace eddy {

// The path runs 1.4 units (half the stroke) inside the 20-unit live area.
static constexpr double kLeft = 3.4, kRight = 20.6, kLead = 0.25, kShown = 1.45;

QPolygonF motionCurve(ZoomSegment::Motion motion) {
    const double omega = cameraOmega(motion);
    auto px = [](double t) { return kLeft + t / kShown * (kRight - kLeft); };
    auto py = [](double value) { return kRight - value * (kRight - kLeft); };
    if (omega == 0)   // Instant: straight up at the step
        return QPolygonF{{px(0), py(0)}, {px(kLead), py(0)}, {px(kLead), py(1)}, {px(kShown), py(1)}};
    QPolygonF curve{{px(0), py(0)}, {px(kLead), py(0)}};
    constexpr int kSteps = 72;
    const double dt = (kShown - kLead) / kSteps;
    double x = 0, v = 0;
    for (int i = 1; i <= kSteps; ++i) {
        springStep(omega, 1, dt, x, v);
        curve << QPointF(px(kLead + i * dt), py(x));
    }
    return curve;
}

namespace {
// Drawn at the real device size on every paint, like theme::tintedIcon.
class MotionEngine : public QIconEngine {
public:
    MotionEngine(ZoomSegment::Motion motion, QColor ink) : m_motion(motion), m_ink(ink) {}
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / 24.0, rect.height() / 24.0);
        painter->setPen(QPen(m_ink, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(motionCurve(m_motion));
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }
    QIconEngine *clone() const override { return new MotionEngine(m_motion, m_ink); }
private:
    ZoomSegment::Motion m_motion;
    QColor m_ink;
};
}

QIcon motionIcon(ZoomSegment::Motion motion, const QColor &ink, int) {
    return QIcon(new MotionEngine(motion, ink));
}

QString motionName(ZoomSegment::Motion motion) {
    switch (motion) {
    case ZoomSegment::Motion::Focused: return QCoreApplication::translate("eddy", "Focused");
    case ZoomSegment::Motion::Smooth: return QCoreApplication::translate("eddy", "Smooth");
    case ZoomSegment::Motion::Instant: return QCoreApplication::translate("eddy", "Instant");
    }
    return {};
}

}
```

`src/zoombar.h`:

```cpp
#pragma once
#include <QWidget>
#include "studiodocument.h"
class QToolButton;
namespace eddy {

// The selected zoom's floating bar at the bottom of the canvas (Q2 = A):
// zoom level, motion, remove. Follow Cursor joins it with S5.
class ZoomBar : public QWidget {
    Q_OBJECT
public:
    explicit ZoomBar(QWidget *parent = nullptr);
    void setZoom(const ZoomSegment &zoom);
    void refreshTheme();
signals:
    void scaleChosen(double scale);
    void motionChosen(ZoomSegment::Motion motion);
    void removeRequested();
private:
    QToolButton *m_scale = nullptr;
    QToolButton *m_motion = nullptr;
};

}
```

`src/zoombar.cpp`:

```cpp
#include "zoombar.h"
#include "motionicon.h"
#include "theme.h"
#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

namespace eddy {

static QMenu *popupMenu(QToolButton *owner, const QString &name) {
    auto *menu = new QMenu(owner);
    menu->setObjectName(name);
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    return menu;
}

ZoomBar::ZoomBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ZoomBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    auto button = [&](const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        layout->addWidget(b);
        return b;
    };
    m_scale = button(QStringLiteral("ZoomScale"), tr("Zoom level · wheel over the map for any level"));
    m_scale->setPopupMode(QToolButton::InstantPopup);
    auto *scales = popupMenu(m_scale, QStringLiteral("ZoomScaleMenu"));
    for (double s : {1.25, 1.5, 2.0, 3.0}) {
        auto *action = scales->addAction(QString::number(s, 'g', 3) + QStringLiteral("×"));
        action->setCheckable(true);
        action->setData(s);
        connect(action, &QAction::triggered, this, [this, s] { emit scaleChosen(s); });
    }
    m_scale->setMenu(scales);
    m_motion = button(QStringLiteral("ZoomMotion"), tr("How the camera moves into and out of this zoom"));
    m_motion->setPopupMode(QToolButton::InstantPopup);
    auto *motions = popupMenu(m_motion, QStringLiteral("ZoomMotionMenu"));
    for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                        ZoomSegment::Motion::Instant}) {
        auto *action = motions->addAction(motionName(motion));
        action->setCheckable(true);
        action->setData(QVariant::fromValue(motion));
        connect(action, &QAction::triggered, this, [this, motion] { emit motionChosen(motion); });
    }
    m_motion->setMenu(motions);
    auto *remove = button(QStringLiteral("ZoomRemove"), tr("Remove this zoom · Delete"));
    remove->setText(tr("Remove"));
    connect(remove, &QToolButton::clicked, this, &ZoomBar::removeRequested);
    refreshTheme();
}

void ZoomBar::setZoom(const ZoomSegment &zoom) {
    theme::setMenuLabel(m_scale, QString::number(zoom.scale, 'g', 3) + QStringLiteral("×"));
    for (QAction *a : m_scale->menu()->actions()) a->setChecked(qFuzzyCompare(a->data().toDouble(), zoom.scale));
    theme::setMenuLabel(m_motion, motionName(zoom.motion));
    for (QAction *a : m_motion->menu()->actions())
        a->setChecked(a->data().value<ZoomSegment::Motion>() == zoom.motion);
    adjustSize();
}

void ZoomBar::refreshTheme() {
    theme::setMenuArrow(m_scale);
    theme::setMenuArrow(m_motion);
    const QColor ink = QApplication::palette().color(QPalette::WindowText);
    for (QAction *a : m_motion->menu()->actions())
        a->setIcon(motionIcon(a->data().value<ZoomSegment::Motion>(), ink, theme::kFsSmall));
}

}
```

`resources/eddy.qss` nach den CropBar-Regeln:

```css
/* The zoom bar shares the crop bar's floating geometry. */
QWidget#ZoomBar { background: @raise1; border: none; border-radius: 12px; }
QWidget#ZoomBar QToolButton {
    color: @sub; padding: 0 8px; border-radius: 8px;
    font-size: @fs-micro; font-weight: 500;
}
QWidget#ZoomBar QToolButton:hover, QWidget#ZoomBar QToolButton:focus { background: @raise2; color: @fg; }
QWidget#ZoomBar QToolButton#ZoomScale { font-family: @font-mono; font-weight: 400; }
QToolButton#ZoomScale::menu-indicator, QToolButton#ZoomMotion::menu-indicator { image: none; width: 0; height: 0; }
QMenu#ZoomScaleMenu, QMenu#ZoomMotionMenu { background: @raise1; color: @sub; border: none; border-radius: 10px; padding: 4px; font-size: @fs-small; font-weight: 500; }
QMenu#ZoomScaleMenu { font-family: @font-mono; font-weight: 400; }
QMenu#ZoomScaleMenu::item, QMenu#ZoomMotionMenu::item { padding: 2px 8px; margin: 1px 0; border: none; border-radius: 6px; background: transparent; }
QMenu#ZoomScaleMenu::item:checked, QMenu#ZoomMotionMenu::item:checked { background: @raise2; color: @fg; }
QMenu#ZoomScaleMenu::item:selected, QMenu#ZoomMotionMenu::item:selected { background: @raise3; color: @fg; }
QMenu#ZoomScaleMenu::indicator, QMenu#ZoomMotionMenu::indicator { image: none; width: 0; height: 0; }
```

- [x] **Step 4: Build und Test.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R zoombar --output-on-failure` · Expected: bestanden.

---

### Task 6: Mini-Karte

**Files:**
- Create: `src/minimap.h`, `src/minimap.cpp`, `tests/test_minimap.cpp`
- Modify: `CMakeLists.txt` (Quelle nach `src/zoombar.cpp`, `eddy_test(test_minimap)` nach `eddy_test(test_zoombar)`), `resources/eddy.qss`

**Interfaces:**
- Produces: `class MiniMap : QWidget { void setContent(const QImage &image, const QRectF &content); void setCamera(const QRectF &camera); signals: dragged(QPointF documentDelta), dragFinished(bool cancelled), wheelZoom(double steps); }`.
  `dragged` meldet, wie weit das Kamerafenster dem Zeiger folgt (gleiche Richtung).
  Größe: Bild höchstens 152 × 112 px im Seitenverhältnis des Inhalts, 4 px Innenrand.

- [x] **Step 1: Test schreiben** `tests/test_minimap.cpp`:

```cpp
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
```

- [x] **Step 2: Test laufen lassen, er scheitert am Build.**

- [x] **Step 3: Umsetzen.** `src/minimap.h`:

```cpp
#pragma once
#include <QImage>
#include <QWidget>
namespace eddy {

// Where the camera looks while a zoom is edited (Q3 = C): the whole content,
// the camera's window bright and the rest dimmed. Dragging moves the window,
// the wheel zooms. Editor chrome only, never exported.
class MiniMap : public QWidget {
    Q_OBJECT
public:
    explicit MiniMap(QWidget *parent = nullptr);
    // `content` is the document rect `image` shows.
    void setContent(const QImage &image, const QRectF &content);
    void setCamera(const QRectF &camera);
signals:
    void dragged(QPointF documentDelta);
    void dragFinished(bool cancelled);
    void wheelZoom(double steps);   // +1 per notch zooms in
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    QRectF imageRect() const { return QRectF(rect()).adjusted(4, 4, -4, -4); }
    QRectF windowRect() const;
    QImage m_image;
    QRectF m_content, m_camera;
    bool m_dragging = false;
    QPointF m_last;
};

}
```

`src/minimap.cpp`:

```cpp
#include "minimap.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QWheelEvent>

namespace eddy {

MiniMap::MiniMap(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("MiniMap"));
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Drag to move the zoom · wheel to change it"));
}

void MiniMap::setContent(const QImage &image, const QRectF &content) {
    m_image = image;
    m_content = content;
    if (!content.isEmpty()) {
        const double scale = qMin(152.0 / content.width(), 112.0 / content.height());
        setFixedSize(qRound(content.width() * scale) + 8, qRound(content.height() * scale) + 8);
    }
    update();
}

void MiniMap::setCamera(const QRectF &camera) {
    m_camera = camera;
    update();
}

QRectF MiniMap::windowRect() const {
    if (m_content.isEmpty() || m_camera.isEmpty()) return {};
    const QRectF inner = imageRect();
    const double sx = inner.width() / m_content.width(), sy = inner.height() / m_content.height();
    return QRectF(inner.left() + (m_camera.left() - m_content.left()) * sx,
                  inner.top() + (m_camera.top() - m_content.top()) * sy,
                  m_camera.width() * sx, m_camera.height() * sy);
}

void MiniMap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF inner = imageRect();
    QPainterPath shape;
    shape.addRoundedRect(inner, 6, 6);
    painter.setClipPath(shape);
    if (!m_image.isNull()) painter.drawImage(inner, m_image);
    else painter.fillRect(inner, palette().color(QPalette::Base));
    const QRectF window = windowRect();
    QColor dim = palette().color(QPalette::Window);
    dim.setAlpha(160);
    QPainterPath outside = shape;
    if (!window.isEmpty()) {
        QPainterPath hole;
        hole.addRect(window);
        outside = outside.subtracted(hole);
    }
    painter.fillPath(outside, dim);
    if (!window.isEmpty()) {
        painter.setClipping(false);
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(window.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

void MiniMap::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    m_dragging = true;
    m_last = event->position();
    setCursor(Qt::ClosedHandCursor);
    setFocus(Qt::MouseFocusReason);
    event->accept();
}

void MiniMap::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging || m_content.isEmpty()) return;
    const QPointF delta = event->position() - m_last;
    m_last = event->position();
    const QRectF inner = imageRect();
    emit dragged(QPointF(delta.x() * m_content.width() / inner.width(),
                         delta.y() * m_content.height() / inner.height()));
    event->accept();
}

void MiniMap::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging || event->button() != Qt::LeftButton) return;
    mouseMoveEvent(event);
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    emit dragFinished(false);
    event->accept();
}

void MiniMap::wheelEvent(QWheelEvent *event) {
    emit wheelZoom(event->angleDelta().y() / 120.0);
    event->accept();
}

void MiniMap::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        emit dragFinished(true);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

}
```

`resources/eddy.qss`:

```css
QWidget#MiniMap { background: @raise1; border: none; border-radius: 10px; }
```

- [x] **Step 4: Build und Test.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R minimap --output-on-failure` · Expected: bestanden.

---

### Task 7: Camera-Seite im Studio-Popover

**Files:**
- Modify: `src/studiopopover.h`, `src/studiopopover.cpp`, `resources/eddy.qss`
- Create: `tests/test_studiopopover.cpp`; `CMakeLists.txt` (`eddy_test(test_studiopopover)` nach `eddy_test(test_studiostyle)`)

**Interfaces:**
- Consumes: `motionIcon`, `motionName` (Task 5).
- Produces: `struct StudioCameraSettings { bool available; std::optional<ZoomSegment::Motion> motion; bool keepZoomedIn; bool keepZoomedInAvailable; }`;
  Konstruktor `StudioPopover(const StudioStyle &, QSize content, const StudioCameraSettings &camera, QWidget *parent)`;
  `void setKeepZoomedInAvailable(bool)`; Signale `motionChosen(ZoomSegment::Motion)`, `keepZoomedInChanged(bool)`.
  Objektnamen: Seiten-Tabs `StudioPage` (Text "Style", "Camera"), `StudioMotion` (3), `StudioKeepZoomed`.

- [x] **Step 1: Test schreiben** `tests/test_studiopopover.cpp`:

```cpp
#include <QtTest>
#include <QToolButton>
#include "studiopopover.h"

using namespace eddy;

static QToolButton *button(QWidget &w, const QString &name, const QString &text) {
    for (auto *b : w.findChildren<QToolButton *>(name))
        if (b->text() == text) return b;
    return nullptr;
}

class TestStudioPopover : public QObject {
    Q_OBJECT
private slots:
    void imagesHaveNoCameraPage() {
        StudioPopover popover(StudioStyle(), QSize(200, 100), {}, nullptr);
        QVERIFY(popover.findChildren<QToolButton *>(QStringLiteral("StudioPage")).isEmpty());
    }
    void cameraPageSetsMotionAndKeepZoomedIn() {
        StudioStyle style;
        style.background = StudioStyle::Background::Color;
        StudioCameraSettings camera;
        camera.available = true;
        camera.motion = ZoomSegment::Motion::Focused;
        StudioPopover popover(style, QSize(1920, 1080), camera, nullptr);
        popover.show();
        const QSize size = popover.size();
        button(popover, QStringLiteral("StudioPage"), QStringLiteral("Camera"))->click();
        QCOMPARE(popover.size(), size);   // pages share one size
        QSignalSpy motions(&popover, &StudioPopover::motionChosen);
        QVERIFY(button(popover, QStringLiteral("StudioMotion"), QStringLiteral("Focused"))->isChecked());
        button(popover, QStringLiteral("StudioMotion"), QStringLiteral("Smooth"))->click();
        QCOMPARE(motions.first().first().value<ZoomSegment::Motion>(), ZoomSegment::Motion::Smooth);
        auto *keep = popover.findChild<QToolButton *>(QStringLiteral("StudioKeepZoomed"));
        QVERIFY(keep && !keep->isEnabled());
        popover.setKeepZoomedInAvailable(true);
        QVERIFY(keep->isEnabled());
        QSignalSpy keeps(&popover, &StudioPopover::keepZoomedInChanged);
        keep->click();
        QCOMPARE(keeps.first().first().toBool(), true);
    }
    void mixedMotionsCheckNone() {
        StudioCameraSettings camera;
        camera.available = true;
        StudioPopover popover(StudioStyle(), QSize(1920, 1080), camera, nullptr);
        for (auto *b : popover.findChildren<QToolButton *>(QStringLiteral("StudioMotion")))
            QVERIFY(!b->isChecked());
    }
};

QTEST_MAIN(TestStudioPopover)
#include "test_studiopopover.moc"
```

- [x] **Step 2: Test laufen lassen, er scheitert am Build.**

- [x] **Step 3: Umsetzen.**

`src/studiopopover.h`: Includes `<optional>`, `"studiodocument.h"`; vor der Klasse:

```cpp
// What the Camera page shows; only videos have one.
struct StudioCameraSettings {
    bool available = false;
    std::optional<ZoomSegment::Motion> motion;   // empty while the zooms differ
    bool keepZoomedIn = false;
    bool keepZoomedInAvailable = false;          // the ratio leaves room to fill
};
```

Konstruktor `StudioPopover(const StudioStyle &style, QSize content, const StudioCameraSettings &camera, QWidget *parent = nullptr);`,
öffentlich `void setKeepZoomedInAvailable(bool available);`, Signale
`void motionChosen(ZoomSegment::Motion motion);` und `void keepZoomedInChanged(bool on);`,
privat `QToolButton *m_keep = nullptr;`.

`src/studiopopover.cpp`: Der bisherige Aufbau landet auf einer Seite. Anfang des
Konstruktors:

```cpp
StudioPopover::StudioPopover(const StudioStyle &style, QSize content, const StudioCameraSettings &camera,
                             QWidget *parent)
    : QWidget(parent, Qt::Popup), m_style(style), m_content(content) {
    setObjectName(QStringLiteral("StudioPopover"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(12);
    auto *pages = new QStackedWidget(this);
    auto *stylePage = new QWidget(pages);
    auto *grid = new QGridLayout(stylePage);
    grid->setContentsMargins(0, 0, 0, 0);
```

Alle bisherigen Widgets der Style-Seite bekommen `stylePage` statt `this` als Eltern
(`new QLabel(text.toUpper(), stylePage)` usw.; die Lambdas `connect(…, this, …)` bleiben).
Nach dem Ratio-Block (vor `apply();`):

```cpp
    pages->addWidget(stylePage);
    if (camera.available) {
        auto *tabs = new QHBoxLayout;
        tabs->setSpacing(4);
        auto *tabGroup = new QButtonGroup(this);
        const QStringList names{tr("Style"), tr("Camera")};
        for (int i = 0; i < names.size(); ++i) {
            auto *tab = new QToolButton(this);
            tab->setObjectName(QStringLiteral("StudioPage"));
            tab->setText(names[i]);
            tab->setCheckable(true);
            tab->setChecked(i == 0);
            tab->setFixedHeight(theme::kFloatButton.height());
            tab->setCursor(Qt::PointingHandCursor);
            tabGroup->addButton(tab, i);
            tabs->addWidget(tab);
        }
        tabs->addStretch(1);
        outer->addLayout(tabs);
        connect(tabGroup, &QButtonGroup::idClicked, pages, &QStackedWidget::setCurrentIndex);

        auto *cameraPage = new QWidget(pages);
        auto *rows = new QGridLayout(cameraPage);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setHorizontalSpacing(4);
        rows->setVerticalSpacing(6);
        auto rowLabel = [&](const QString &text, int row) {
            auto *label = new QLabel(text, cameraPage);
            label->setObjectName(QStringLiteral("StudioLabel"));
            rows->addWidget(label, row, 0);
        };
        rowLabel(tr("Motion"), 0);
        auto *motions = new QButtonGroup(this);
        const QColor ink = QApplication::palette().color(QPalette::WindowText);
        int column = 1;
        for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                            ZoomSegment::Motion::Instant}) {
            auto *b = new QToolButton(cameraPage);
            b->setObjectName(QStringLiteral("StudioMotion"));
            b->setText(motionName(motion));
            b->setIcon(motionIcon(motion, ink, 16));
            b->setIconSize(QSize(16, 16));
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setCheckable(true);
            b->setChecked(camera.motion == motion);
            b->setFixedHeight(theme::kFloatButton.height());
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(tr("Every zoom moves like this; new zooms too"));
            motions->addButton(b);
            rows->addWidget(b, 0, column++);
            connect(b, &QToolButton::clicked, this, [this, motion] { emit motionChosen(motion); });
        }
        // Mixed motions: nothing is checked until the user picks one.
        motions->setExclusive(camera.motion.has_value());
        connect(motions, &QButtonGroup::buttonClicked, this, [motions] { motions->setExclusive(true); });
        rowLabel(tr("Zoom"), 1);
        m_keep = new QToolButton(cameraPage);
        m_keep->setObjectName(QStringLiteral("StudioKeepZoomed"));
        m_keep->setText(tr("Keep zoomed in"));
        m_keep->setCheckable(true);
        m_keep->setChecked(camera.keepZoomedIn);
        m_keep->setFixedHeight(theme::kFloatButton.height());
        m_keep->setCursor(Qt::PointingHandCursor);
        rows->addWidget(m_keep, 1, 1, 1, 3, Qt::AlignLeft);
        connect(m_keep, &QToolButton::toggled, this, &StudioPopover::keepZoomedInChanged);
        setKeepZoomedInAvailable(camera.keepZoomedInAvailable);
        rows->setRowStretch(2, 1);
        pages->addWidget(cameraPage);
    }
    outer->addWidget(pages);
```

Methode:

```cpp
void StudioPopover::setKeepZoomedInAvailable(bool available) {
    if (!m_keep) return;
    m_keep->setEnabled(available);
    m_keep->setToolTip(available ? tr("Fill the frame's ratio with a window of the video")
                                 : tr("Needs an output ratio that differs from the video"));
}
```

Includes: `<QStackedWidget>`, `<QVBoxLayout>`, `"motionicon.h"`.

`resources/eddy.qss` (Seiten-Tabs wie die übrigen Popover-Knöpfe; nur die Schrift
eine Stufe größer, damit sie als Überschrift liest):

```css
QWidget#StudioPopover QToolButton#StudioPage { font-size: @fs-small; font-weight: 600; }
```

`src/editorwindow.cpp`, `openStudio`: vorläufig
`new StudioPopover(m_studioStyle, size, StudioCameraSettings{}, this)` (Task 9 füllt die Werte).

- [x] **Step 4: Build und Tests.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R "studiopopover|editorwindow" --output-on-failure` · Expected: bestanden.

---

### Task 8: Export mit "keep zoomed in"

**Files:**
- Modify: `src/videoexporter.h`, `src/videoexporter.cpp`
- Test: `tests/test_videoexporter.cpp`

**Interfaces:**
- Produces: `QRect VideoExportRequest::baseView` (null = der ganze Crop). Muss im Crop (bzw. im
  Bild) liegen und gerade Koordinaten haben. Ohne Zooms ist er ein zusätzlicher Crop im
  Filtergraph; mit Zooms ist er die Basis der Kamera im Render-Pfad.

- [x] **Step 1: Tests schreiben** (in `TestVideoExporter`):

```cpp
    void keepZoomedInCropsToTheFramesRatio() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage halves(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&halves);
            p.fillRect(0, 0, 160, 180, Qt::red);
            p.fillRect(160, 0, 160, 180, Qt::blue);
        }
        const QString still = dir.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "1",
            "-r", "30", "-vf", "setsar=1,format=yuv420p", input}));
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        request.studio.background = StudioStyle::Background::Color;
        request.studio.color = Qt::green;
        request.studio.padding = 0;
        request.studio.radius = 0;
        request.studio.shadow = 0;
        request.studio.aspect = QSize(9, 16);
        request.baseView = keepZoomedInRect(QRect(0, 0, 320, 180), request.studio, QPointF(240, 90));
        QCOMPARE(request.baseView, QRect(190, 0, 100, 180));
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY(probe.ok);
        QCOMPARE(probe.info.size, QSize(102, 180));   // 9:16 with a 2 px background grow
        QCOMPARE(qRound(probe.info.fps), 30);         // a static crop stays on the filter graph
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        const QColor c = QImage(frame).pixelColor(51, 90);
        QVERIFY2(c.blue() > 200 && c.red() < 60, qPrintable(c.name()));

        // With a zoom the renderer takes over and zooms inside the same window.
        request.outputPath = dir.filePath(QStringLiteral("zoom.mp4"));
        request.baseView = QRect(110, 0, 100, 180);    // straddles both halves
        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(135, 90),
                          ZoomSegment::Motion::Instant}};
        const DeliverResult zoomed = writeVideoWithOverlay(request);
        QVERIFY2(zoomed.ok, qPrintable(zoomed.error));
        const auto zoomProbe = probeVideoFile(request.outputPath);
        QCOMPARE(zoomProbe.info.size, QSize(102, 180));
        QCOMPARE(qRound(zoomProbe.info.fps), 60);
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        // 2x around x = 135 shows 110..160 (red) across the whole width.
        const QImage decoded(frame);
        for (int x : {8, 51, 94}) {
            const QColor p = decoded.pixelColor(x, 90);
            QVERIFY2(p.red() > 200 && p.blue() < 60, qPrintable(QStringLiteral("%1: %2").arg(x).arg(p.name())));
        }
    }
    void rejectsABaseViewOutsideTheCrop() {
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        VideoExportRequest request{QStringLiteral("in.mp4"), QStringLiteral("out.mp4"), overlay};
        request.cropRect = QRect(0, 0, 160, 180);
        request.baseView = QRect(100, 0, 100, 180);
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("view")), qPrintable(result.error));
    }
```

(`#include "studiostyle.h"` in der Testdatei, falls noch nicht da.)

- [x] **Step 2: Tests laufen lassen, sie scheitern am Build.**

- [x] **Step 3: Umsetzen.**

`src/videoexporter.h` in `VideoExportRequest` nach `QVector<ZoomSegment> zooms;`:

```cpp
    // "Always keep zoomed in" (studio plan 6.5): the unzoomed view inside the
    // crop, shaped like the output frame. Null: the whole crop.
    QRect baseView;
```

`src/videoexporter.cpp`, `writeVideo` nach der Crop-Prüfung:

```cpp
    const QRect bounds = crop.isNull() ? req.overlay.rect() : crop;
    const QRect view = req.baseView;
    if (view != QRect() && (view.isEmpty() || !bounds.contains(view)
        || view.x() % 2 || view.y() % 2 || view.width() % 2 || view.height() % 2)) {
        r.error = QStringLiteral("video base view must fit the crop and use even pixel coordinates");
        return r;
    }
    // What the frame holds when the camera rests: the base view, else the crop.
    const QRect framed = view.isNull() ? crop : view;
```

`contentSize` wird `const QSize contentSize = framed.isNull() ? req.overlay.size() : framed.size();`,
und im Filtergraph `if (!framed.isNull()) filter += …crop… .arg(framed.width()).arg(framed.height()).arg(framed.x()).arg(framed.y());`.

`writeRendered`:

```cpp
    const QRect content = req.cropRect.isNull() ? req.overlay.rect() : req.cropRect;
    const QRect view = req.baseView.isNull() ? content : req.baseView;
    const CameraPath camera(req.zooms, TimeMap(source.durationMs, req.trimInMs, endMs, {}),
                            CameraFrame{QRectF(content),
                                        view == content ? 0.0 : double(view.width()) / view.height(),
                                        QRectF(view).center()});
    const StudioRenderer renderer(req.overlay.size(), view, req.studio,
                                  overlayVisible ? req.overlay : QImage());
```

- [x] **Step 4: Build und Tests.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel -R videoexporter --output-on-failure` · Expected: alle bestanden.

---

### Task 9: EditorWindow verdrahten

**Files:**
- Modify: `src/editorwindow.h`, `src/editorwindow.cpp`
- Create: `tests/test_studiozooms.cpp`; `CMakeLists.txt` (`eddy_test(test_studiozooms)` nach `eddy_test(test_editorwindow)`)
- Modify: `tests/test_editorwindow.cpp` (`chromeFollowsTheDensityRules`: `"ZoomBar"` in die Liste der Flächen)

**Interfaces:**
- Consumes: alles aus Task 1 bis 8.
- Produces (`EditorWindow`, öffentlich): `StudioDocument studioDocument() const`,
  `void setStudioDocument(const StudioDocument &)` (kein Undo-Schritt), `quint32 selectedZoom() const`,
  `void selectZoom(quint32 id)`, `QRect cameraBase() const`.

- [x] **Step 1: Tests schreiben** `tests/test_studiozooms.cpp`:

```cpp
#include <QtTest>
#include <QAudioOutput>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QMediaPlayer>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QToolButton>
#include <QUndoStack>
#include <QVideoSink>
#include "canvas.h"
#include "editorwindow.h"
#include "minimap.h"
#include "studiopopover.h"
#include "videotimeline.h"
#include "zoombar.h"

using namespace eddy;

static bool have(const QString &cmd) { return !QStandardPaths::findExecutable(cmd).isEmpty(); }

static bool runProcess(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// 4 s, 320x180: a grey grid with a red marker at (240, 60).
static QString markerClip(const QTemporaryDir &dir) {
    QImage image(320, 180, QImage::Format_RGB32);
    image.fill(QColor(60, 60, 60));
    {
        QPainter p(&image);
        p.setPen(QColor(120, 120, 120));
        for (int x = 0; x < 320; x += 20) p.drawLine(x, 0, x, 179);
        for (int y = 0; y < 180; y += 20) p.drawLine(0, y, 319, y);
        p.fillRect(236, 56, 8, 8, Qt::red);
    }
    const QString still = dir.filePath(QStringLiteral("marker.png"));
    image.save(still);
    const QString clip = dir.filePath(QStringLiteral("marker.mp4"));
    runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-loop", "1", "-i", still, "-t", "4",
        "-r", "30", "-vf", "setsar=1,format=yuv420p", "-c:v", "libx264", "-g", "15", clip});
    return clip;
}

static MediaDocument videoDoc(const QString &path) {
    MediaDocument doc;
    doc.kind = MediaKind::Video;
    doc.path = path;
    doc.video = {QSize(320, 180), 4000, 30.0};
    return doc;
}

static StudioStyle dusk() {
    StudioStyle s;
    s.background = StudioStyle::Background::Color;
    s.color = QColor(40, 40, 120);
    return s;
}

class TestStudioZooms : public QObject {
    Q_OBJECT
private slots:
    void laneFollowsStudio() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        auto *timeline = w.findChild<VideoTimeline *>();
        QVERIFY(!timeline->zoomLaneVisible());
        w.setStudioStyle(dusk());
        QVERIFY(timeline->zoomLaneVisible());
        w.setStudioStyle(StudioStyle());
        QVERIFY(!timeline->zoomLaneVisible());
    }
    void zAddsASelectedZoomInOneUndoStep() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(900, 640);
        w.show();
        auto *undo = w.findChild<QUndoStack *>();
        const int before = undo->count();
        QTest::keyClick(&w, Qt::Key_Z);
        const StudioDocument doc = w.studioDocument();
        QCOMPARE(doc.zooms.size(), 1);
        QCOMPARE(doc.zooms.first().startMs, 0);
        QCOMPARE(doc.zooms.first().endMs, 2000);
        QCOMPARE(doc.zooms.first().scale, 2.0);
        QCOMPARE(doc.zooms.first().point, QPointF(160, 90));
        QCOMPARE(w.selectedZoom(), doc.zooms.first().id);
        QCOMPARE(undo->count(), before + 1);
        QVERIFY(w.findChild<VideoTimeline *>()->zoomLaneVisible());
        QTRY_VERIFY(w.findChild<ZoomBar *>()->isVisible());
        // The canvas shows where the zoom settles, not the whole frame.
        const QRectF target(80, 45, 160, 90);
        QCOMPARE(w.findChild<Canvas *>()->camera(), target);
        undo->undo();
        QVERIFY(w.studioDocument().zooms.isEmpty());
        QCOMPARE(w.selectedZoom(), 0u);
        QVERIFY(!w.findChild<ZoomBar *>()->isVisible());
        QVERIFY(w.findChild<Canvas *>()->camera().isEmpty());
    }
    void escapeDeselectsBeforeClosingAndDeleteRemoves() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        QVERIFY(w.selectedZoom());
        QTest::keyClick(&w, Qt::Key_Escape);
        QCOMPARE(w.selectedZoom(), 0u);
        QVERIFY(w.isVisible());
        w.selectZoom(w.studioDocument().zooms.first().id);
        QTest::keyClick(&w, Qt::Key_Right, Qt::ShiftModifier);   // ten frames at 30 fps
        QCOMPARE(w.studioDocument().zooms.first().startMs, 333);
        QTest::keyClick(&w, Qt::Key_Delete);
        QVERIFY(w.studioDocument().zooms.isEmpty());
    }
    void zoomBarAndMiniMapEditTheSelectedZoom() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(900, 640);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        w.findChild<QMenu *>(QStringLiteral("ZoomScaleMenu"))->actions().at(3)->trigger();
        QCOMPARE(w.studioDocument().zooms.first().scale, 3.0);
        w.findChild<QMenu *>(QStringLiteral("ZoomMotionMenu"))->actions().at(1)->trigger();
        QCOMPARE(w.studioDocument().zooms.first().motion, ZoomSegment::Motion::Smooth);
        QCOMPARE(undo->count(), steps + 2);
        auto *map = w.findChild<MiniMap *>();
        QTRY_VERIFY(map->isVisible());
        // Two wheel notches, one undo step.
        emit map->wheelZoom(-1);
        emit map->wheelZoom(-1);
        QVERIFY(qAbs(w.studioDocument().zooms.first().scale - 3.0 / 1.21) < 1e-9);
        QCOMPARE(undo->count(), steps + 3);
        // Dragging the map moves the target with it, clamped like the camera.
        emit map->dragged(QPointF(40, 0));
        emit map->dragFinished(false);
        QCOMPARE(w.studioDocument().zooms.first().point.x(), 200.0);
        QCOMPARE(undo->count(), steps + 4);
        w.findChild<QToolButton *>(QStringLiteral("ZoomRemove"))->click();
        QVERIFY(w.studioDocument().zooms.isEmpty());
    }
    void canvasDragMovesTheTargetAgainstThePointer() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(900, 640);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        auto *canvas = w.findChild<Canvas *>();
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        const QPoint start = canvas->mapFromScene(QPointF(160, 90));
        const double scale = canvas->transform().m11();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas->viewport(), start + QPoint(-20, 0));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(-20, 0));
        // The content follows the pointer, so the camera moved right.
        QVERIFY(qAbs(w.studioDocument().zooms.first().point.x() - (160 + 20 / scale)) < 0.5);
        QCOMPARE(undo->count(), steps + 1);
    }
    void cameraPageSetsEveryZoomAndKeepZoomedInNarrowsTheView() {
        QTemporaryDir dir;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, cli);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        StudioDocument doc = w.studioDocument();
        doc.style = dusk();
        doc.style.aspect = QSize(9, 16);
        w.setStudioDocument(doc);
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        w.openStudio();
        auto *popover = w.findChild<StudioPopover *>();
        QVERIFY(popover);
        for (auto *b : popover->findChildren<QToolButton *>(QStringLiteral("StudioMotion")))
            if (b->text() == QStringLiteral("Instant")) b->click();
        auto *keep = popover->findChild<QToolButton *>(QStringLiteral("StudioKeepZoomed"));
        QVERIFY(keep->isEnabled());
        keep->click();
        popover->close();
        QTRY_VERIFY(!w.findChild<StudioPopover *>());
        QCOMPARE(undo->count(), steps + 1);
        QCOMPARE(w.studioDocument().motion, ZoomSegment::Motion::Instant);
        QCOMPARE(w.studioDocument().zooms.first().motion, ZoomSegment::Motion::Instant);
        QVERIFY(w.studioDocument().keepZoomedIn);
        const QRect base = w.cameraBase();
        QCOMPARE(base.height(), 180);
        QVERIFY(base.width() < 120);
        QCOMPARE(w.findChild<Canvas *>()->contentRect(), QRectF(base));
    }
    void exportedZoomMatchesThePreview() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString clip = markerClip(dir);
        QVERIFY(QFileInfo::exists(clip));
        Config cfg; cfg.animations = false; cfg.copyOnSave = false;
        CliOptions cli; cli.output.toFile = true; cli.output.filePath = dir.filePath(QStringLiteral("out.mp4"));
        EditorWindow w(videoDoc(clip), cfg, cli);
        w.resize(900, 640);
        w.show();
        QTRY_VERIFY(w.findChild<QMediaPlayer *>());
        auto *player = w.findChild<QMediaPlayer *>();
        player->audioOutput()->setMuted(true);
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        QTRY_VERIFY(player->isSeekable());
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        StudioDocument doc;
        doc.zooms = {{1, 1000, 3000, 2.0, ZoomSegment::Target::Point, QPointF(240, 60),
                      ZoomSegment::Motion::Focused}};
        w.setStudioDocument(doc);
        auto *canvas = w.findChild<Canvas *>();
        auto *timeline = w.findChild<VideoTimeline *>();
        // Where the marker sits in the content, as a fraction of the content's size.
        auto previewMarker = [&](qint64 ms) -> QPointF {
            player->pause();
            timeline->setPosition(ms);
            emit timeline->seekRequested(ms);
            if (!QTest::qWaitFor([&] { return qAbs(w.property("presentedStart").toLongLong() - ms) < 20; }, 5000))
                return QPointF(-3, w.property("presentedStart").toLongLong());
            QTest::qWait(50);
            const QImage shot = canvas->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
            const QRectF camera = canvas->camera().isEmpty() ? QRectF(0, 0, 320, 180) : canvas->camera();
            const QRectF hole(canvas->mapFromScene(camera.topLeft()), canvas->mapFromScene(camera.bottomRight()));
            double sx = 0, sy = 0; int n = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c.red() > 180 && c.green() < 90 && c.blue() < 90) { sx += x; sy += y; ++n; }
                }
            return n ? QPointF((sx / n - hole.left()) / hole.width(), (sy / n - hole.top()) / hole.height())
                     : QPointF(-1, -1);
        };
        const QPointF before = previewMarker(500), during = previewMarker(1200), settled = previewMarker(2500);
        w.save();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(cli.output.filePath), 20000);
        auto exportMarker = [&](double seconds) -> QPointF {
            const QString frame = dir.filePath(QStringLiteral("frame-%1.png").arg(seconds));
            runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", QString::number(seconds),
                                                  "-i", cli.output.filePath, "-frames:v", "1", frame});
            const QImage image(frame);
            double sx = 0, sy = 0; int n = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() > 180 && c.green() < 90 && c.blue() < 90) { sx += x; sy += y; ++n; }
                }
            return n ? QPointF(sx / n / image.width(), sy / n / image.height()) : QPointF(-2, -2);
        };
        // Within about one output pixel of the 320x180 frame (studio plan 7.2).
        auto same = [](QPointF a, QPointF b) { return qAbs(a.x() - b.x()) * 320 <= 1.5 && qAbs(a.y() - b.y()) * 180 <= 1.5; };
        const QPointF exported[] = {exportMarker(0.5), exportMarker(1.2), exportMarker(2.5)};
        const QPointF previewed[] = {before, during, settled};
        for (int i = 0; i < 3; ++i)
            QVERIFY2(same(previewed[i], exported[i]),
                     qPrintable(QStringLiteral("%1: preview %2,%3 export %4,%5").arg(i)
                         .arg(previewed[i].x()).arg(previewed[i].y()).arg(exported[i].x()).arg(exported[i].y())));
        // The zoom really moved the marker: 2x around it puts it in the middle.
        QVERIFY(qAbs(settled.x() - 0.5) < 0.05 && qAbs(settled.y() - 0.5) < 0.05);
    }
};

QTEST_MAIN(TestStudioZooms)
#include "test_studiozooms.moc"
```

Für den Test bekommt `EditorWindow` eine Qt-Property `presentedStart` (die Startzeit des
zuletzt gezeigten Frames): `Q_PROPERTY(qint64 presentedStart MEMBER m_presentedStart)`.

- [x] **Step 2: Tests laufen lassen, sie scheitern am Build.**

- [x] **Step 3: Umsetzen.**

`src/editorwindow.h`:
- Includes `"studiodocument.h"`, `"camerapath.h"`, `"timemap.h"`; Vorwärtsdeklarationen `class ZoomBar; class MiniMap;`.
- `Q_PROPERTY(qint64 presentedStart MEMBER m_presentedStart)` nach `Q_OBJECT`.
- Öffentlich:

```cpp
    StudioStyle studioStyle() const { return m_studio.style; }
    void setStudioStyle(const StudioStyle &style);   // not an undo step on its own
    StudioDocument studioDocument() const { return m_studio; }
    void setStudioDocument(const StudioDocument &doc);   // not an undo step on its own
    quint32 selectedZoom() const { return m_selectedZoom; }
    void selectZoom(quint32 id);
    // The unzoomed view: the crop, narrowed by "keep zoomed in".
    QRect cameraBase() const;
```

- Privat:

```cpp
    void editStudio(const std::function<void(StudioDocument &)> &change, int mergeKey = 0);
    QRect cameraContent() const;
    QRectF currentCamera() const;
    void rebuildCamera();
    void updateCamera();
    void refreshZoomUi();
    void positionZoomUi();
    void addZoomAt(qint64 sourceMs);
    void moveCameraTarget(QPointF delta, bool preview);
    void finishCameraGesture(bool cancelled);
    StudioDocument m_studio;          // this document; off by default
    quint32 m_selectedZoom = 0;
    QPointF m_lastZoomPoint;          // where a new zoom points first
    TimeMap m_timeMap;
    CameraPath m_cameraPath;
    ZoomBar *m_zoomBar = nullptr;
    MiniMap *m_miniMap = nullptr;
    bool m_cameraGesture = false;
    bool m_showBaseView = false;      // while "keep zoomed in" is dragged
    StudioDocument m_cameraGestureBefore;
```

`StudioStyle m_studioStyle;` entfällt; alle Stellen lesen `m_studio.style`.

`src/editorwindow.cpp` (Includes `"zoomlane.h"`, `"zoombar.h"`, `"minimap.h"`):

```cpp
void EditorWindow::setStudioStyle(const StudioStyle &style) {
    StudioDocument doc = m_studio;
    doc.style = style;
    setStudioDocument(doc);
}

void EditorWindow::setStudioDocument(const StudioDocument &doc) {
    m_studio = doc;
    m_toolbar->setStudioActive(doc.style.active());
    if (zoomlane::indexOf(m_studio.zooms, m_selectedZoom) < 0) m_selectedZoom = 0;
    setCropRect(m_cropRect);   // the base view follows ratio and "keep zoomed in"
    if (isVideo()) onVideoContentChanged();
}

void EditorWindow::editStudio(const std::function<void(StudioDocument &)> &change, int mergeKey) {
    StudioDocument after = m_studio;
    change(after);
    if (after == m_studio) return;
    m_undo->push(new SetStudioDocumentCommand(m_studio, after,
        [this](const StudioDocument &d) { setStudioDocument(d); }, mergeKey));
}

QRect EditorWindow::cameraContent() const {
    return m_cropRect.isEmpty() ? QRect(QPoint(), m_media.nativeSize()) : m_cropRect;
}

QRect EditorWindow::cameraBase() const {
    const QRect content = cameraContent();
    return isVideo() && m_studio.keepZoomedIn
        ? keepZoomedInRect(content, m_studio.style, m_studio.keepCenter) : content;
}
```

`setCropRect` wird:

```cpp
void EditorWindow::setCropRect(QRect rect) {
    m_cropRect = rect;
    const QRect base = cameraBase();
    m_canvas->setContentRect(base == QRect(QPoint(), m_media.nativeSize()) ? QRect() : base);
    updateStudioPreview();
    rebuildCamera();
    // (Fenstertitel wie bisher)
}
```

`updateStudioPreview` benutzt `cameraBase()` statt `content` (für Bilder ist das der Crop).
Der Crop-Abbruch (`CropController::cancelled`) ruft statt `m_canvas->setContentRect(m_cropRect)`
jetzt `setCropRect(m_cropRect)` vor `restoreView`. Der Crop-Start ruft vor
`m_beforeCropView = m_canvas->transform();` `m_canvas->setCamera({});` und blendet
`m_zoomBar`/`m_miniMap` aus (`refreshZoomUi()` prüft `m_crop->active()`).

Kamera:

```cpp
void EditorWindow::rebuildCamera() {
    if (!isVideo() || !m_timeline) return;
    const QRect content = cameraContent(), base = cameraBase();
    m_timeMap = TimeMap(m_media.video.durationMs, m_trimInMs, m_trimOutMs, m_studio.fragments);
    m_cameraPath = CameraPath(m_studio.zooms, m_timeMap,
        CameraFrame{QRectF(content), base == content ? 0.0 : double(base.width()) / base.height(),
                    QRectF(base).center()});
    m_timeline->setZoomLaneVisible(m_studio.style.active() || !m_studio.zooms.isEmpty());
    m_timeline->setZooms(m_studio.zooms, [this](qint64 source) {
        const auto out = m_timeMap.toOutput(double(source));
        return out ? m_cameraPath.zoomAt(*out) : 1.0;
    });
    m_timeline->setSelectedZoom(m_selectedZoom);
    updateCamera();
    refreshZoomUi();
}

QRectF EditorWindow::currentCamera() const {
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    if (m_showBaseView) return QRectF(cameraBase());
    const int selected = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
    if (!playing && selected >= 0) return m_cameraPath.targetRect(m_studio.zooms[selected]);
    const qint64 source = playing && m_presentedStart >= 0 ? m_presentedStart : m_timeline->position();
    const double out = source <= m_trimInMs ? 0.0 : m_timeMap.toOutputAfter(double(source));
    return m_cameraPath.rectAt(out);
}

void EditorWindow::updateCamera() {
    if (!isVideo() || !m_timeline) return;
    m_canvas->setCamera(m_crop && m_crop->active() ? QRectF() : currentCamera());
    if (m_miniMap && m_miniMap->isVisible()) m_miniMap->setCamera(currentCamera());
}
```

Aufrufe von `updateCamera()`: am Ende des `videoFrameChanged`-Lambdas; im
`playbackStateChanged`-Lambda zusätzlich `refreshZoomUi();`; in `setTrimRangeState` und im
`durationChanged`-Lambda `rebuildCamera();`; im `positionChanged`-Lambda bei pausiertem Player.

Zoom-UI (im Konstruktor nach `setupCrop()`, nur für Video):

```cpp
    if (isVideo()) {
        m_zoomBar = new ZoomBar(m_canvas->viewport());
        m_zoomBar->hide();
        m_miniMap = new MiniMap(m_canvas->viewport());
        m_miniMap->hide();
        connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionZoomUi);
        connect(m_zoomBar, &ZoomBar::scaleChosen, this, [this](double scale) {
            editStudio([&](StudioDocument &d) {
                d.zooms[zoomlane::indexOf(d.zooms, m_selectedZoom)].scale = scale;
            });
        });
        connect(m_zoomBar, &ZoomBar::motionChosen, this, [this](ZoomSegment::Motion motion) {
            editStudio([&](StudioDocument &d) {
                d.zooms[zoomlane::indexOf(d.zooms, m_selectedZoom)].motion = motion;
            });
        });
        connect(m_zoomBar, &ZoomBar::removeRequested, this, [this] {
            editStudio([&](StudioDocument &d) { d.zooms.remove(zoomlane::indexOf(d.zooms, m_selectedZoom)); });
        });
        connect(m_miniMap, &MiniMap::dragged, this, [this](QPointF d) { moveCameraTarget(d, true); });
        connect(m_miniMap, &MiniMap::dragFinished, this, &EditorWindow::finishCameraGesture);
        connect(m_miniMap, &MiniMap::wheelZoom, this, [this](double steps) {
            const int i = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
            if (i < 0) return;
            const double scale = qBound(1.1, m_studio.zooms[i].scale * std::pow(1.1, steps), 4.0);
            editStudio([&](StudioDocument &d) { d.zooms[i].scale = scale; }, int(m_selectedZoom));
        });
        connect(m_canvas, &Canvas::cameraDragged, this, [this](QPointF d) { moveCameraTarget(-d, true); });
        connect(m_canvas, &Canvas::cameraDragFinished, this, &EditorWindow::finishCameraGesture);
    }
```

In `createPlaybackBar` nach den übrigen Timeline-Verbindungen:

```cpp
    connect(m_timeline, &VideoTimeline::zoomAddRequested, this, &EditorWindow::addZoomAt);
    connect(m_timeline, &VideoTimeline::zoomSelected, this, &EditorWindow::selectZoom);
    connect(m_timeline, &VideoTimeline::zoomsPreviewed, this, [this](const QVector<ZoomSegment> &zooms) {
        m_studio.zooms = zooms;   // the drag's own preview; the edit is one undo step at release
        rebuildCamera();
    });
    connect(m_timeline, &VideoTimeline::zoomsEdited, this,
            [this](const QVector<ZoomSegment> &before, const QVector<ZoomSegment> &after) {
        StudioDocument from = m_studio, to = m_studio;
        from.zooms = before;
        to.zooms = after;
        m_undo->push(new SetStudioDocumentCommand(from, to, [this](const StudioDocument &d) { setStudioDocument(d); }));
    });
    connect(m_timeline, &VideoTimeline::zoomMenuRequested, this, [this](quint32 id, QPoint global) {
        selectZoom(id);
        QMenu menu(this);
        for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth, ZoomSegment::Motion::Instant})
            menu.addAction(motionIcon(motion, palette().color(QPalette::WindowText), theme::kFsSmall),
                           motionName(motion), this, [this, id, motion] {
                editStudio([&](StudioDocument &d) { d.zooms[zoomlane::indexOf(d.zooms, id)].motion = motion; });
            });
        menu.addSeparator();
        menu.addAction(tr("Remove zoom · Delete"), this, [this, id] {
            editStudio([&](StudioDocument &d) { d.zooms.remove(zoomlane::indexOf(d.zooms, id)); });
        });
        menu.exec(global);
    });
```

Weitere Methoden:

```cpp
void EditorWindow::selectZoom(quint32 id) {
    if (zoomlane::indexOf(m_studio.zooms, id) < 0) id = 0;
    m_selectedZoom = id;
    if (m_timeline) m_timeline->setSelectedZoom(id);
    updateCamera();
    refreshZoomUi();
}

void EditorWindow::addZoomAt(qint64 sourceMs) {
    const auto span = zoomlane::placeNew(m_studio.zooms, sourceMs, m_media.video.durationMs);
    if (!span) {
        m_toast->showMessage(tr("No room for a zoom here"));
        return;
    }
    ZoomSegment z;
    z.id = zoomlane::nextId(m_studio.zooms);
    z.startMs = span->first;
    z.endMs = span->second;
    z.point = m_lastZoomPoint.isNull() ? QRectF(cameraBase()).center() : m_lastZoomPoint;
    z.motion = m_studio.motion;
    editStudio([&](StudioDocument &d) { zoomlane::insert(d.zooms, z); });
    if (m_player) m_player->pause();
    selectZoom(z.id);
}

void EditorWindow::moveCameraTarget(QPointF delta, bool) {
    if (!m_cameraGesture) {
        m_cameraGesture = true;
        m_cameraGestureBefore = m_studio;
    }
    const int i = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
    if (i >= 0) {
        ZoomSegment &z = m_studio.zooms[i];
        z.point += delta;
        z.point = m_cameraPath.targetRect(z).center();   // no drift past the edges
        m_lastZoomPoint = z.point;
    } else if (m_studio.keepZoomedIn) {
        const QRectF content(cameraContent());
        const QSizeF view = QRectF(cameraBase()).size();
        const QPointF c = m_studio.keepCenter + delta;
        m_studio.keepCenter = QPointF(qBound(content.left() + view.width() / 2, c.x(), content.right() - view.width() / 2),
                                      qBound(content.top() + view.height() / 2, c.y(), content.bottom() - view.height() / 2));
        m_showBaseView = true;
        setCropRect(m_cropRect);
        return;
    }
    rebuildCamera();
}

void EditorWindow::finishCameraGesture(bool cancelled) {
    if (!m_cameraGesture) return;
    m_cameraGesture = false;
    m_showBaseView = false;
    const StudioDocument before = m_cameraGestureBefore, after = m_studio;
    if (cancelled || before == after) { setStudioDocument(before); return; }
    m_undo->push(new SetStudioDocumentCommand(before, after, [this](const StudioDocument &d) { setStudioDocument(d); }));
}

void EditorWindow::refreshZoomUi() {
    if (!m_zoomBar) return;
    const bool cropping = m_crop && m_crop->active();
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const int i = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
    const bool keep = m_studio.keepZoomedIn && cameraBase() != cameraContent();
    if (i >= 0 && !cropping) m_zoomBar->setZoom(m_studio.zooms[i]);
    m_zoomBar->setVisible(i >= 0 && !cropping);
    const bool map = !cropping && !playing && (i >= 0 || keep);
    if (map) {
        const QImage frame = m_lastVideoFrame.isValid() ? m_lastVideoFrame.toImage() : m_bg;
        const QRect content = cameraContent();
        m_miniMap->setContent(frame.isNull() ? QImage()
            : frame.copy(content).scaled(304, 224, Qt::KeepAspectRatio, Qt::SmoothTransformation), QRectF(content));
        m_miniMap->setCamera(currentCamera());
    }
    m_miniMap->setVisible(map);
    m_canvas->setCameraDragEnabled(map);
    positionZoomUi();
}

void EditorWindow::positionZoomUi() {
    if (!m_zoomBar) return;
    const QSize viewport = m_canvas->viewport()->size();
    m_zoomBar->adjustSize();
    const QRect bar(QPoint((viewport.width() - m_zoomBar->width()) / 2, viewport.height() - m_zoomBar->height() - 12),
                    m_zoomBar->size());
    m_zoomBar->move(bar.topLeft());
    // Bottom right inside the content; above the bar when they would meet.
    const QRectF camera = m_canvas->camera().isEmpty() ? m_canvas->contentRect() : m_canvas->camera();
    const QRect content = QRect(m_canvas->mapFromScene(camera.topLeft()), m_canvas->mapFromScene(camera.bottomRight()))
                              .intersected(QRect(QPoint(), viewport));
    QRect map(QPoint(content.right() - m_miniMap->width() - 12, content.bottom() - m_miniMap->height() - 12),
              m_miniMap->size());
    if (m_zoomBar->isVisible() && map.intersects(bar.adjusted(-6, -6, 6, 6))) map.moveBottom(bar.top() - 12);
    m_miniMap->move(map.topLeft());
    m_zoomBar->raise();
    m_miniMap->raise();
}
```

Tasten in `keyPressEvent` (vor dem `switch`):

```cpp
    if (isVideo() && m_timeline && e->modifiers() == Qt::NoModifier && e->key() == Qt::Key_Z) {
        addZoomAt(m_timeline->position());
        e->accept();
        return;
    }
    if (m_selectedZoom && m_scene->selectedItems().isEmpty()) {
        const int i = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
        if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && i >= 0) {
            editStudio([&](StudioDocument &d) { d.zooms.remove(i); });
            e->accept();
            return;
        }
        if ((e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) && i >= 0) {
            const double fps = m_media.video.fps > 0 ? m_media.video.fps : 30.0;
            const int frames = (e->key() == Qt::Key_Left ? -1 : 1)
                * (e->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1);
            const qint64 start = m_studio.zooms[i].startMs + qRound64(frames * 1000.0 / fps);
            editStudio([&](StudioDocument &d) { zoomlane::move(d.zooms, m_selectedZoom, start, m_media.video.durationMs); },
                       0x10000 + int(m_selectedZoom));
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Escape) {
        if (m_canvas->cameraDragging()) { m_canvas->cancelCameraDrag(); e->accept(); return; }
        if (m_timeline && m_timeline->zoomDragging()) { m_timeline->cancelInteraction(); e->accept(); return; }
        if (m_selectedZoom) { selectZoom(0); e->accept(); return; }
    }
```

(Die Pfeiltasten verschieben den Zoom nur, wenn keine Annotation gewählt ist; sonst bleibt es
beim Verschieben der Annotation.)

Popover (`openStudio`):

```cpp
    const StudioDocument before = m_studio;
    if (!m_studio.style.active()) setStudioStyle(loadLastStudioStyle(configPath()));
    StudioCameraSettings camera;
    camera.available = isVideo();
    camera.motion = m_studio.motion;
    for (const ZoomSegment &z : std::as_const(m_studio.zooms))
        if (z.motion != m_studio.motion) camera.motion.reset();
    camera.keepZoomedIn = m_studio.keepZoomedIn;
    camera.keepZoomedInAvailable = keepZoomedInRect(cameraContent(), m_studio.style,
                                                    QRectF(cameraContent()).center()) != cameraContent();
    auto *popover = new StudioPopover(m_studio.style, cameraContent().size(), camera, this);
    …
    connect(popover, &StudioPopover::styleChanged, this, [this, popover](const StudioStyle &style) {
        setStudioStyle(style);
        popover->setKeepZoomedInAvailable(keepZoomedInRect(cameraContent(), style,
            QRectF(cameraContent()).center()) != cameraContent());
    });
    connect(popover, &StudioPopover::motionChosen, this, [this](ZoomSegment::Motion motion) {
        StudioDocument doc = m_studio;
        doc.motion = motion;
        for (ZoomSegment &z : doc.zooms) z.motion = motion;
        setStudioDocument(doc);
    });
    connect(popover, &StudioPopover::keepZoomedInChanged, this, [this](bool on) {
        StudioDocument doc = m_studio;
        doc.keepZoomedIn = on;
        if (on && doc.keepCenter.isNull()) doc.keepCenter = QRectF(cameraContent()).center();
        setStudioDocument(doc);
    });
    connect(popover, &QObject::destroyed, this, [this, before] {
        if (m_studio == before) return;
        if (m_studio.style != before.style) saveLastStudioStyle(configPath(), m_studio.style);
        m_undo->push(new SetStudioDocumentCommand(before, m_studio,
            [this](const StudioDocument &d) { setStudioDocument(d); }));
    });
```

Export (`startVideoExportCache`):

```cpp
    request.studio = m_studio.style;
    request.zooms = m_studio.zooms;
    if (cameraBase() != cameraContent()) request.baseView = cameraBase();
```

`hasVideoEdits`: `|| !m_studio.zooms.isEmpty() || cameraBase() != cameraContent()` ergänzen.

Frame kopieren (`copyVideoFrame`), statt der Zeile mit `renderStudioImage(…)`:

```cpp
    const QRect base = cameraBase();
    const QRectF view = m_canvas->camera().isEmpty() ? QRectF(base) : m_canvas->camera();
    const QImage visible = image.copy(view.toAlignedRect())
        .scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QApplication::clipboard()->setImage(renderStudioImage(visible, m_studio.style));
```

`toggleTheme`: `if (m_zoomBar) m_zoomBar->refreshTheme();`.

`tests/test_editorwindow.cpp`, `chromeFollowsTheDensityRules`: `"ZoomBar"` zur Flächenliste.

- [x] **Step 4: Build und Tests.** Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel --output-on-failure` · Expected: alle bestanden.

---

### Task 10: Vorschau-Renders, Doku, Abnahme

**Files:**
- Modify: `tools/eddy_preview.cpp`, `docs/plans/2026-09-24-studio-features.md`, `README.md`, `docs/specs/2026-09-23-studio-mode.md`

- [x] **Step 1: Vorschau-Modi.** In `tools/eddy_preview.cpp` nach dem Studio-Block:

```cpp
    if (mode.contains(QStringLiteral("zoom")) || mode.contains(QStringLiteral("camera"))) {
        // A video with Studio on, one zoom selected (context bar and mini map)
        // and, for "camera", the popover's Camera page.
        eddy::StudioDocument doc = window->studioDocument();
        doc.style = eddy::loadLastStudioStyle(QString());
        doc.zooms = {{1, 1000, 3000, 2.0, eddy::ZoomSegment::Target::Point,
                      QPointF(window->cameraBase().width() * 0.7, window->cameraBase().height() * 0.35),
                      eddy::ZoomSegment::Motion::Focused},
                     {2, 5000, 6500, 1.5, eddy::ZoomSegment::Target::Point,
                      QPointF(window->cameraBase().center()), eddy::ZoomSegment::Motion::Smooth}};
        window->setStudioDocument(doc);
        window->selectZoom(1);
        app.processEvents();
        pm = window->grab();
        if (mode.contains(QStringLiteral("camera"))) {
            window->openStudio();
            app.processEvents();
            auto *popover = window->findChild<QWidget *>(QStringLiteral("StudioPopover"));
            for (auto *tab : popover->findChildren<QToolButton *>(QStringLiteral("StudioPage")))
                if (tab->text() == QStringLiteral("Camera")) tab->click();
            app.processEvents();
            pm = window->grab();
            QPainter painter(&pm);
            painter.drawPixmap(window->mapFromGlobal(popover->pos()), popover->grab());
        }
    }
```

- [x] **Step 2: Renders erzeugen und ansehen** (dunkel und hell, normal und schmal):

```sh
ffmpeg -v error -y -ss 24 -t 8 -i ~/Bilder/boltsnap/boltsnap-2026-09-24_16-30-37.mp4 -an -c:v libx264 -pix_fmt yuv420p /tmp/eddy-zoom-clip.mp4
for t in dark light; do
  QT_QPA_PLATFORM=offscreen build-rel/eddy_preview /tmp/eddy-s2-zoom-$t.png $t video-file-zoom /tmp/eddy-zoom-clip.mp4
  QT_QPA_PLATFORM=offscreen build-rel/eddy_preview /tmp/eddy-s2-camera-$t.png $t video-file-camera /tmp/eddy-zoom-clip.mp4
  QT_QPA_PLATFORM=offscreen build-rel/eddy_preview /tmp/eddy-s2-narrow-$t.png $t video-file-zoom-narrow /tmp/eddy-zoom-clip.mp4
done
```

Geprüft wird: Spur mit Kurve und Label, gewählter Block heller mit Griffen, Kontextleiste
unten mittig, Mini-Karte unten rechts ohne Überschneidung, Camera-Seite mit drei
Kurvensymbolen, schmales Fenster ohne abgeschnittene Leiste.

- [x] **Step 3: Echter Export-Durchlauf** mit dem Clip und zwei Zooms über die App-Logik
  (Test `exportedZoomMatchesThePreview` deckt die Geometrie ab; zusätzlich ein Export des
  echten Clips über einen kleinen Aufruf von `writeVideoWithOverlay` mit `baseView` und 9:16,
  Frames bei 0,5 s, 2 s und 3,5 s ansehen).

- [x] **Step 4: Doku.** Gesamtplan: N6 entschieden (Focused 100/20, Smooth 36/12), S2 als
  umgesetzt mit Testzahlen, Hinweis auf diesen Plan. README: Zoom-Segmente, Tasten `Z`,
  Camera-Seite. Spec: Status.

- [x] **Step 5: Abnahme.**

Run: `cmake --build build-rel --parallel 2 && ctest --test-dir build-rel && git diff --check`
Expected: alle Tests bestanden, keine Whitespace-Fehler.

## Umsetzung und Abweichungen (2026-09-24)

Alle Tasks umgesetzt, `ctest` 39/39, `git diff --check` sauber. Abweichungen vom Plan oben:

- Task 1: `keepZoomedInRect` gibt den Inhalt unverändert zurück, wenn das Verhältnis schon
  passt (nicht nur ohne gesetztes Verhältnis).
- Task 4: Der Test prüfte, dass `resetZoom` nach einem mausverankerten Zoom zurück an die
  alte Stelle führt; das stimmt auch ohne Kamera nicht. Er prüft jetzt, dass `setCamera({})`
  die Ansicht exakt zurückgibt und dass der eigene Zoom bei aktiver Kamera erhalten bleibt
  (4× mit Kamera, 2× danach). Der Drag-Test wählt das Move-Werkzeug (Standard ist Arrow).
- Task 7: Die Seiten liegen nicht in einem `QStackedWidget` (das rechnet die Mindestgröße
  aller Seiten ein), sondern direkt im Layout, nur eine sichtbar. Jede Seite bekommt ihre
  eigene Größe, die rechte Kante bleibt unter dem Studio-Knopf. Die Größenanzeige zeigt den
  gerahmten Basisausschnitt (`setContentSize`).
- Task 9: `QTRY_*` passt nicht in Lambdas mit Rückgabewert; der Vorschau-Test wartet mit
  `QTest::qWaitFor` und erst, wenn der Player seekbar ist und ein Frame zeigt. Neu dazu:
  `playbackRowMakesRoomForTheLane`, `cameraPageShowsTheZoomsSharedMotionAndTheFramedSize`,
  `undoDuringALaneDragCancelsTheDrag`.
- Mini-Karte: Breite ein Viertel des sichtbaren Inhalts, 72 bis 152 px (`setWidthLimit`);
  das Bild ist `m_bg`, also der Frame in Dokument-Orientierung.
- Review (`/code-review high`) fand und es wurde behoben: Kantenschwenk-Timer mit veraltetem
  Zeiger beim Halten eines Zooms; ohne Studio lief der gezoomte Inhalt über den Inhaltsrand
  hinaus; `setCamera` meldete `viewChanged` nicht (schwebende Leisten blieben stehen);
  Mini-Karte ohne Drehung; Größenanzeige ohne "keep zoomed in"; Motion-Markierung bei
  gleichen, aber vom Standard abweichenden Zooms; Undo während eines Spur-Ziehens
  (`cancelGestures`); doppelte Motion-Namen und Zoom-Edits (`editZoom`, `removeZoom`).
  Kamera-Neuberechnung: nur bei geänderten Eingaben (`CameraInputs`), beim Ziel-Ziehen gar
  nicht (erst am Ende), beim Spur-Ziehen höchstens alle 16 ms. Gegenproben: jeder Fix
  zurückgebaut macht seinen Test rot.
- `test_crop::videoCoordinatesAgree` las die Zwischenablage, bevor eine aufgeschobene
  Frame-Kopie fertig war (einmal unter `ctest` rot, einzeln achtmal grün); der Test leert
  die Zwischenablage jetzt und wartet auf das neue Bild.
- Messung: Vorschau gegen Export am Markierungsclip vor/mitten in/nach der Fahrt 0 / 0,4 / 0 px.
  Echter Clip 9:16 mit zwei Zooms (5,4 s, 660×1170) in 1,9 s exportiert; Frames angesehen.
- Nicht geprüft: Klicks in einem echten Wayland-Fenster; nur Offscreen-Renders und Tests.

## Abdeckung gegen den Gesamtplan

| Gesamtplan | Task |
| --- | --- |
| 3.2 Undo `SetStudioDocumentCommand` | 1 |
| 3.4 Federwerte N6 | entschieden, Global Constraints |
| 5 Kamera im Canvas, Takt pro Video-Frame, pausiert gleiche Auswertung | 4, 9 |
| 6.1 Spur Q1 = C, Einfügen, Ziehen, Einrasten, Tasten, Rechtsklick | 2, 3, 9 |
| 6.1 Kontextleiste Q2 = A | 5, 9 |
| 6.1 Ziel im Canvas Q3 = C, Mini-Karte | 4, 6, 9 |
| 6.1 Tests: Einfügen, Klemmen, Einrasten, Undo, Cache-Neubau, Export-Markierung | 2, 3, 8, 9 |
| 6.5 keep zoomed in mit festem Mittelpunkt | 1, 8, 9 |
| 7.2 Vorschau gleich Export | 9 (`exportedZoomMatchesThePreview`) |
| 11 Q4/Q4b/Q4d Seiten, Motion-Symbole | 5, 7 |
| `eddy_preview`-Modi, Renders beider Themes, schmales Fenster | 10 |
