# Studio S1: Render-Exportpfad (Implementierungsplan)

> **Für die Umsetzung:** Task für Task, jede Task mit ihrem Test grün, bevor die nächste
> beginnt. Schritte als Checkboxen (`- [ ]`). **Keine Commits ohne ausdrücklichen Auftrag**;
> statt Commit-Schritten gibt es Checkpoints.

**Ziel:** Ein zweiter Videoexport, der jedes Frame mit Qt zeichnet (Kamera, Studio-Rahmen,
Annotationen), aktiv sobald ein Dokument Zoom-Segmente hat. Ohne Zooms bleibt der heutige
Filtergraph-Export unverändert.

**Architektur:** `StudioRenderer` zeichnet ein Ausgabeframe (reines QPainter, threadsicher).
`runRenderPipeline` verbindet einen ffmpeg-Decoder (eigener Thread, rawvideo bgra auf stdout),
das Zeichnen (Aufrufer-Thread) und einen ffmpeg-Encoder (eigener Thread, rawvideo auf stdin)
über zwei nach Bytes begrenzte Warteschlangen. `writeVideoWithOverlay` schaltet bei
`VideoExportRequest::zooms` auf diesen Pfad um; `writeVideoRendered` erzwingt ihn für den
Vergleichstest. Blur bleibt ffmpeg-Sache (im Decoder, dieselbe Filterkette wie heute), die
Kamera kommt aus `CameraPath` (S0), der Ton direkt aus der Quelle in den Encoder.
Grundlage: `docs/plans/2026-09-24-studio-features.md`, Abschnitte 2 und 4.

**Tech Stack:** C++20, Qt 6 (QPainter, QProcess), `std::thread`, ffmpeg-Pipes, QtTest.

## Global Constraints

- Qt-Untergrenze: Linux-CI mit Distro-Qt 6, Windows-CI Qt 6.8.3, lokal 6.11.1. Nur
  plattformneutrale Mittel (`QProcess`, `std::thread`), kein `popen`.
- Keine neuen Abhängigkeiten. Builds mit `cmake --build build-rel --parallel 2`.
- Ohne Zooms bitgleiches Verhalten des heutigen Exports: dieselben ffmpeg-Argumente,
  dieselben Tests grün.
- Fehlertexte wie beim Filtergraph: "video export cancelled", "ffmpeg export timed out",
  "ffmpeg stopped making progress", sonst ffmpegs letzte stderr-Zeile.
- Ausgabe des Renderpfads immer 60 fps, BT.709 konvertiert und markiert.
- Der Cursor ist Boltsnaps eingebrannter Cursor; der Renderpfad zeichnet keinen.
- Nichts committen, keine Subagents, `git diff --check` sauber.

## Geprüft vor dem Schreiben

Der gesamte Code unten wurde in einer Kopie des Working Trees (`/tmp/eddy-s1`) gebaut und
getestet: Build Exitcode 0, `ctest` 33/33. Gegenproben: Kamera ignoriert → Zoom-Test rot;
BT.709-Matrix weggelassen → Farbmarkierungs-Test rot. Gemessene Abweichung Renderpfad gegen
Filtergraph (gleicher Crop, Blur, Annotation, Studio-Rahmen): Mittel 0,6/255, an den
Messpunkten höchstens 4/255. Leistung am echten Clip `boltsnap-2026-09-21_18-25-58.mp4`
(1894×1026, 240 fps, 5 s ab Sekunde 5, Ausgabe 2058×1190 mit Dusk-Rahmen, 300 Frames,
Hardware-Encoder): Filtergraph 2,42 bis 2,54 s, Renderpfad mit 2×-Zoom 2,76 bis 2,79 s,
also etwa 1,8-fache Echtzeit. Frames angesehen: vor dem Segment Vollbild im Rahmen, im
Segment der 2×-Ausschnitt mit runden Ecken.

## Dateien

| Datei | Verantwortung |
| --- | --- |
| `src/studiorenderer.h/.cpp` (neu) | ein Ausgabeframe: Kamera-Ausschnitt, Annotationen in eigener Auflösung, Rahmen mit Loch darüber |
| `src/renderexport.h/.cpp` (neu) | `RenderPipeline`, `runRenderPipeline`: Threads, Warteschlangen, Abbruch, Stall, Fortschritt |
| `src/videoexporter.h/.cpp` | `VideoExportRequest::zooms`, `writeVideoRendered`, Umschalten, gemeinsame Helfer |
| `tests/test_studiorenderer.cpp`, `tests/test_renderexport.cpp` (neu) | Renderer und Pipeline |
| `tests/test_videoexporter.cpp` | Vergleich mit dem Filtergraph, Zoom, Trim mit Ton |
| `CMakeLists.txt` | zwei Quellen, zwei Tests |

---

### Task 1: Frame-Renderer

**Files:**
- Create: `src/studiorenderer.h`, `src/studiorenderer.cpp`, `tests/test_studiorenderer.cpp`
- Modify: `CMakeLists.txt` (`src/studiorenderer.cpp` nach `src/camerapath.cpp`; `eddy_test(test_studiorenderer)` nach `eddy_test(test_camerapath)`)

**Interfaces:**
- Consumes: `StudioStyle`, `studioLayout`, `renderStudioBackground`, `renderStudioFrameMask` (`src/studiostyle.h`).
- Produces: `class StudioRenderer { StudioRenderer(QSize source, QRect content, const StudioStyle &, const QImage &overlay); QSize outputSize() const; void render(const QImage &frame, const QRectF &camera, QImage &out) const; }`

- [x] **Step 1: Test schreiben** `tests/test_studiorenderer.cpp`:

```cpp
#include <QtTest>
#include <QImage>
#include <QPainter>
#include "studiorenderer.h"

using namespace eddy;

// Left half red, right half blue, top-right quarter marked green.
static QImage source() {
    QImage image(200, 100, QImage::Format_RGB32);
    QPainter p(&image);
    p.fillRect(0, 0, 100, 100, Qt::red);
    p.fillRect(100, 0, 100, 100, Qt::blue);
    p.fillRect(150, 0, 50, 50, Qt::green);
    return image;
}

static bool near(QColor a, QColor b, int tolerance = 8) {
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance
        && qAbs(a.blue() - b.blue()) <= tolerance;
}

static QImage draw(const StudioRenderer &renderer, const QImage &frame, const QRectF &camera) {
    QImage out(renderer.outputSize(), QImage::Format_RGB32);
    renderer.render(frame, camera, out);
    return out;
}

class TestStudioRenderer : public QObject {
    Q_OBJECT
private slots:
    void aStillCameraCopiesTheContent() {
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), StudioStyle(), QImage());
        QCOMPARE(renderer.outputSize(), QSize(200, 100));
        const QImage out = draw(renderer, source(), QRectF(0, 0, 200, 100));
        QVERIFY(near(out.pixelColor(50, 50), Qt::red));
        QVERIFY(near(out.pixelColor(120, 80), Qt::blue));
        QVERIFY(near(out.pixelColor(175, 25), Qt::green));
    }

    void theCameraMagnifiesItsWindow() {
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), StudioStyle(), QImage());
        const QImage out = draw(renderer, source(), QRectF(100, 0, 100, 50));   // the top-right quarter, 2x
        QVERIFY(near(out.pixelColor(40, 50), Qt::blue));
        QVERIFY(near(out.pixelColor(160, 50), Qt::green));
        QVERIFY(near(out.pixelColor(199, 0), Qt::green));
    }

    void aCropSetsTheOutputButNotTheCamera() {
        const StudioRenderer renderer(QSize(200, 100), QRect(100, 0, 100, 50), StudioStyle(), QImage());
        QCOMPARE(renderer.outputSize(), QSize(100, 50));
        const QImage out = draw(renderer, source(), QRectF(100, 0, 100, 50));
        QVERIFY(near(out.pixelColor(20, 25), Qt::blue));
        QVERIFY(near(out.pixelColor(80, 25), Qt::green));
    }

    void framesTheContentLikeTheFilterGraph() {
        StudioStyle style;
        style.background = StudioStyle::Background::Color;
        style.color = QColor(255, 255, 0);
        style.padding = 10;
        style.radius = 20;
        style.shadow = 0;
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), style, QImage());
        const StudioLayout layout = studioLayout(QSize(200, 100), style);
        QCOMPARE(renderer.outputSize(), layout.output);
        const QImage out = draw(renderer, source(), QRectF(0, 0, 200, 100));
        QVERIFY(near(out.pixelColor(2, 2), QColor(255, 255, 0)));
        const QPoint origin = layout.content.topLeft();
        QVERIFY(near(out.pixelColor(origin + QPoint(50, 50)), Qt::red));
        // A 20 px corner radius: the content's own corner shows the background.
        QVERIFY(near(out.pixelColor(origin + QPoint(1, 1)), QColor(255, 255, 0)));
        QVERIFY(near(out.pixelColor(origin + QPoint(20, 1)), Qt::red));
    }

    void annotationsFollowTheCameraAtTheirOwnResolution() {
        // Twice the document size: annotations stay sharp when the camera zooms.
        QImage overlay(400, 200, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QPainter(&overlay).fillRect(QRect(260, 20, 20, 20), Qt::white);   // document (130..140, 10..20)
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), StudioStyle(), overlay);
        const QImage still = draw(renderer, source(), QRectF(0, 0, 200, 100));
        QVERIFY(near(still.pixelColor(135, 15), Qt::white));
        const QImage zoomed = draw(renderer, source(), QRectF(100, 0, 100, 50));
        QVERIFY(near(zoomed.pixelColor(70, 30), Qt::white));    // (135 - 100) * 2, 15 * 2
        QVERIFY(near(zoomed.pixelColor(40, 30), Qt::blue));
    }
};

QTEST_GUILESS_MAIN(TestStudioRenderer)
#include "test_studiorenderer.moc"
```

In `CMakeLists.txt` nach `eddy_test(test_camerapath)`: `eddy_test(test_studiorenderer)`.

- [x] **Step 2: Scheitern prüfen**

Run: `cmake -S . -B build-rel >/dev/null && cmake --build build-rel --parallel 2 --target test_studiorenderer`
Expected: Build-Fehler `studiorenderer.h: Datei oder Verzeichnis nicht gefunden`.

- [x] **Step 3: Implementieren**

`src/studiorenderer.h`:

```cpp
#pragma once
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include "studiostyle.h"

namespace eddy {

// One output frame of the frame-rendered video export: the camera's window
// of the source and its annotations, framed like the filter-graph export
// (studio plan 4.2). Safe to use from several threads at once.
class StudioRenderer {
public:
    // `source` is the decoded frame size in document pixels, `content` the crop
    // (or the whole frame), `overlay` the annotations over the whole document at
    // any resolution, or null.
    StudioRenderer(QSize source, QRect content, const StudioStyle &style, const QImage &overlay);

    QSize outputSize() const { return m_output; }
    // `camera` is in document pixels; `out` must be outputSize().
    void render(const QImage &frame, const QRectF &camera, QImage &out) const;

private:
    QSize m_source;
    QSize m_output;
    QRectF m_target;   // where the camera's window lands in the output
    QImage m_frame;    // background with a rounded hole, drawn over the content
    QImage m_overlay;
};

}
```

`src/studiorenderer.cpp`:

```cpp
#include "studiorenderer.h"
#include <QPainter>

namespace eddy {

StudioRenderer::StudioRenderer(QSize source, QRect content, const StudioStyle &style, const QImage &overlay)
    : m_source(source), m_overlay(overlay) {
    const StudioLayout layout = studioLayout(content.size(), style);
    m_output = layout.output;
    m_target = QRectF(layout.content);
    if (style.active()) {
        // The same background and coverage mask the filter graph merges, as one
        // image with the mask for alpha: opaque around the content, clear over it.
        m_frame = renderStudioBackground(content.size(), style)
                      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_frame.setAlphaChannel(renderStudioFrameMask(content.size(), style));
    }
}

void StudioRenderer::render(const QImage &frame, const QRectF &camera, QImage &out) const {
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(m_target, frame, camera);
    if (!m_overlay.isNull()) {
        const qreal sx = qreal(m_overlay.width()) / m_source.width();
        const qreal sy = qreal(m_overlay.height()) / m_source.height();
        p.drawImage(m_target, m_overlay,
                    QRectF(camera.x() * sx, camera.y() * sy, camera.width() * sx, camera.height() * sy));
    }
    if (!m_frame.isNull()) p.drawImage(0, 0, m_frame);
}

}
```

In `CMakeLists.txt` nach `    src/camerapath.cpp`: `    src/studiorenderer.cpp`.

- [x] **Step 4: Bestehen prüfen**

Run: `cmake --build build-rel --parallel 2 --target test_studiorenderer && ctest --test-dir build-rel -R '^test_studiorenderer$' --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [x] **Step 5: Checkpoint** `git diff --check` ohne Ausgabe. Kein Commit.

---

### Task 2: Render-Pipeline

**Files:**
- Create: `src/renderexport.h`, `src/renderexport.cpp`, `tests/test_renderexport.cpp`
- Modify: `CMakeLists.txt` (`src/renderexport.cpp` nach `src/studiorenderer.cpp`; `eddy_test(test_renderexport)` nach `eddy_test(test_studiorenderer)`)

**Interfaces:**
- Consumes: `DeliverResult` (`src/exporter.h`).
- Produces: `struct RenderPipeline { QString ffmpeg; QStringList decodeArgs; QSize sourceSize; QStringList encodeArgs; QSize outputSize; qint64 expectedFrames; std::function<void(qint64, const QImage &, QImage &)> render; int timeoutMs; int stallTimeoutMs; std::function<void(int)> progress; std::function<bool()> cancelled; }`, `DeliverResult runRenderPipeline(const RenderPipeline &)`.

- [x] **Step 1: Test schreiben** `tests/test_renderexport.cpp`:

```cpp
#include <QtTest>
#include <QElapsedTimer>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "renderexport.h"

using namespace eddy;

static QString ffmpeg() { return QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); }

static QByteArray run(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(20000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return {};
    return p.readAllStandardOutput().trimmed();
}

// One second of a 30 fps test pattern, decoded at 60 fps: 60 frames.
static RenderPipeline pipeline(const QString &output) {
    RenderPipeline job;
    job.ffmpeg = ffmpeg();
    job.sourceSize = QSize(160, 120);
    job.decodeArgs = {"-hide_banner", "-loglevel", "error", "-nostdin", "-f", "lavfi", "-i",
                      "testsrc=s=160x120:d=1:r=30", "-vf", "fps=60,format=bgra",
                      "-f", "rawvideo", "-pix_fmt", "bgra", "-"};
    job.outputSize = QSize(80, 60);
    job.encodeArgs = {"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "bgra",
                      "-s", "80x60", "-r", "60", "-i", "-", "-c:v", "libx264", "-preset", "veryfast",
                      "-pix_fmt", "yuv420p", output};
    job.expectedFrames = 60;
    // First half second red, then blue: every output frame is ours.
    job.render = [](qint64 index, const QImage &, QImage &out) { out.fill(index < 30 ? Qt::red : Qt::blue); };
    return job;
}

static QColor frameColor(const QString &video, double seconds) {
    const QByteArray png = run(ffmpeg(), {"-v", "error", "-ss", QString::number(seconds), "-i", video,
                                          "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"});
    const QImage image = QImage::fromData(png);
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}

class TestRenderExport : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (ffmpeg().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
            QSKIP("ffmpeg/ffprobe not available");
    }

    void everyFrameIsDrawnByQt() {
        QTemporaryDir dir;
        const QString out = dir.filePath(QStringLiteral("out.mp4"));
        RenderPipeline job = pipeline(out);
        QList<int> reported;
        job.progress = [&](int percent) { reported.append(percent); };
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(run(QStringLiteral("ffprobe"), {"-v", "error", "-count_frames", "-select_streams", "v:0",
                 "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", out}), QByteArray("60"));
        QVERIFY(frameColor(out, 0.2).red() > 200);
        QVERIFY(frameColor(out, 0.8).blue() > 200);
        QVERIFY(!reported.isEmpty());
        QVERIFY(std::is_sorted(reported.cbegin(), reported.cend()));
        QVERIFY2(reported.last() >= 90 && reported.last() <= 99, qPrintable(QString::number(reported.last())));
    }

    void cancellingStopsBothProcesses() {
        QTemporaryDir dir;
        RenderPipeline job = pipeline(dir.filePath(QStringLiteral("out.mp4")));
        job.decodeArgs[job.decodeArgs.indexOf(QStringLiteral("testsrc=s=160x120:d=1:r=30"))]
            = QStringLiteral("testsrc=s=160x120:d=600:r=30");
        job.render = [](qint64, const QImage &, QImage &out) {
            out.fill(Qt::red);
            QThread::msleep(5);
        };
        QElapsedTimer timer;
        timer.start();
        job.cancelled = [&] { return timer.elapsed() > 300; };
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("cancelled")), qPrintable(result.error));
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
    }

    void anEncoderThatStopsReadingIsStopped() {
        QTemporaryDir dir;
        RenderPipeline job = pipeline(dir.filePath(QStringLiteral("out.mp4")));
        // Never reads stdin, so the frames pile up and nothing is ever written.
        job.encodeArgs = {"-hide_banner", "-loglevel", "error", "-nostdin", "-re", "-f", "lavfi",
                          "-i", "nullsrc=d=600", "-f", "null", "-"};
        job.outputSize = QSize(1280, 720);   // big frames fill the pipe at once
        job.stallTimeoutMs = 500;
        QElapsedTimer timer;
        timer.start();
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("progress")), qPrintable(result.error));
        QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
    }

    void ffmpegErrorsReachTheCaller() {
        QTemporaryDir dir;
        RenderPipeline job = pipeline(dir.filePath(QStringLiteral("out.mp4")));
        job.encodeArgs[job.encodeArgs.indexOf(QStringLiteral("libx264"))] = QStringLiteral("no_such_encoder");
        QElapsedTimer timer;
        timer.start();
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY(!result.ok);
        // ffmpeg's own words: "Unknown encoder …" or "… Encoder not found", by version.
        QVERIFY2(result.error.contains(QStringLiteral("encoder"), Qt::CaseInsensitive), qPrintable(result.error));
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestRenderExport)
#include "test_renderexport.moc"
```

In `CMakeLists.txt` nach `eddy_test(test_studiorenderer)`: `eddy_test(test_renderexport)`.

- [x] **Step 2: Scheitern prüfen**

Run: `cmake -S . -B build-rel >/dev/null && cmake --build build-rel --parallel 2 --target test_renderexport`
Expected: Build-Fehler `renderexport.h: Datei oder Verzeichnis nicht gefunden`.

- [x] **Step 3: Implementieren**

`src/renderexport.h`:

```cpp
#pragma once
#include <QImage>
#include <QSize>
#include <QStringList>
#include <functional>
#include "exporter.h"

namespace eddy {

// Decode -> Qt -> encode through two ffmpeg pipes. Decoder and encoder each run
// on their own thread and the caller's thread draws, so the three overlap;
// done one after another they miss real time at 1080p60 (studio plan 2).
struct RenderPipeline {
    QString ffmpeg;
    QStringList decodeArgs;     // must write rawvideo bgra frames of sourceSize to stdout
    QSize sourceSize;
    QStringList encodeArgs;     // must read rawvideo bgra frames of outputSize from stdin
    QSize outputSize;
    qint64 expectedFrames = 0;  // for progress; 0 while unknown
    // Draws output frame `index` from the decoded `source`.
    std::function<void(qint64 index, const QImage &source, QImage &output)> render;
    int timeoutMs = -1;
    // Nothing reaching the encoder this long stops the export.
    int stallTimeoutMs = 60 * 1000;
    std::function<void(int percent)> progress;   // 0-99, or -1 while unknown
    std::function<bool()> cancelled;
};

// Errors read like the filter-graph export's: "video export cancelled",
// "ffmpeg export timed out", "ffmpeg stopped making progress", or ffmpeg's own
// last line of stderr.
DeliverResult runRenderPipeline(const RenderPipeline &pipeline);

}
```

`src/renderexport.cpp`:

```cpp
#include "renderexport.h"
#include <QElapsedTimer>
#include <QProcess>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace eddy {

namespace {

constexpr int kPollMs = 250;
constexpr qint64 kQueueBytes = 128LL * 1024 * 1024;   // per queue; bounds 4K memory use

// A bounded hand-over of frames between two threads.
class FrameQueue {
public:
    explicit FrameQueue(qint64 frameBytes)
        : m_capacity(std::max<qint64>(2, kQueueBytes / std::max<qint64>(1, frameBytes))) {}

    enum class Push { Done, Full, Closed };
    Push push(QImage &frame, int waitMs) {
        std::unique_lock lock(m_mutex);
        if (!m_space.wait_for(lock, std::chrono::milliseconds(waitMs),
                              [&] { return m_closed || qint64(m_frames.size()) < m_capacity; }))
            return Push::Full;
        if (m_closed) return Push::Closed;
        m_frames.push_back(std::move(frame));
        m_ready.notify_one();
        return Push::Done;
    }
    // Empty on timeout; a null image once the producer finished or the queue closed.
    std::optional<QImage> pop(int waitMs) {
        std::unique_lock lock(m_mutex);
        if (!m_ready.wait_for(lock, std::chrono::milliseconds(waitMs),
                              [&] { return m_finished || m_closed || !m_frames.empty(); }))
            return std::nullopt;
        if (m_frames.empty() || m_closed) return QImage();
        QImage frame = std::move(m_frames.front());
        m_frames.pop_front();
        m_space.notify_one();
        return frame;
    }
    void finish() {   // the producer is done; the consumer drains what is left
        std::lock_guard lock(m_mutex);
        m_finished = true;
        m_ready.notify_all();
    }
    void close() {    // abandon both ends
        std::lock_guard lock(m_mutex);
        m_closed = true;
        m_ready.notify_all();
        m_space.notify_all();
    }

private:
    const qint64 m_capacity;
    std::mutex m_mutex;
    std::condition_variable m_ready, m_space;
    std::deque<QImage> m_frames;
    bool m_finished = false, m_closed = false;
};

// The last non-empty line ffmpeg wrote to stderr.
QString lastError(QProcess &process) {
    const QList<QByteArray> lines = process.readAllStandardError().trimmed().split('\n');
    return lines.isEmpty() ? QString() : QString::fromUtf8(lines.last()).trimmed();
}

// Waits for `process` to exit, killing it when `stop` turns true.
void finishProcess(QProcess &process, const std::atomic_bool &stop) {
    while (process.state() != QProcess::NotRunning && !stop) process.waitForFinished(kPollMs);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(5000);
    }
}

bool succeeded(const QProcess &process) {
    return process.error() != QProcess::FailedToStart && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
}

}

DeliverResult runRenderPipeline(const RenderPipeline &job) {
    QElapsedTimer elapsed;
    elapsed.start();
    const qint64 sourceBytes = qint64(job.sourceSize.width()) * job.sourceSize.height() * 4;
    const qint64 outputBytes = qint64(job.outputSize.width()) * job.outputSize.height() * 4;
    FrameQueue decoded(sourceBytes), rendered(outputBytes);
    std::atomic_bool stop = false;
    std::atomic<qint64> written = 0;
    std::atomic_bool encoderDone = false;
    QString decodeError, encodeError;   // each written only by its own thread before join

    std::thread decoder([&] {
        QProcess process;
        process.start(job.ffmpeg, job.decodeArgs);
        if (!process.waitForStarted()) {
            decodeError = QStringLiteral("cannot start ffmpeg: ") + process.errorString();
            decoded.finish();
            return;
        }
        while (!stop) {
            QImage frame(job.sourceSize, QImage::Format_RGB32);   // bgra bytes, opaque
            char *bits = reinterpret_cast<char *>(frame.bits());
            qint64 got = 0;
            while (got < sourceBytes && !stop) {
                if (process.bytesAvailable() == 0 && !process.waitForReadyRead(kPollMs)) {
                    if (process.state() == QProcess::NotRunning) break;
                    continue;
                }
                got += process.read(bits + got, sourceBytes - got);
            }
            if (got < sourceBytes) break;
            FrameQueue::Push pushed;
            while ((pushed = decoded.push(frame, kPollMs)) == FrameQueue::Push::Full && !stop) {}
            if (pushed != FrameQueue::Push::Done) break;
        }
        finishProcess(process, stop);
        if (!stop && !succeeded(process)) decodeError = lastError(process);
        decoded.finish();
    });

    std::thread encoder([&] {
        QProcess process;
        process.start(job.ffmpeg, job.encodeArgs);
        if (!process.waitForStarted()) {
            encodeError = QStringLiteral("cannot start ffmpeg: ") + process.errorString();
            rendered.close();
            encoderDone = true;
            return;
        }
        while (!stop) {
            std::optional<QImage> frame = rendered.pop(kPollMs);
            if (!frame) continue;
            if (frame->isNull()) break;
            process.write(reinterpret_cast<const char *>(frame->constBits()), frame->sizeInBytes());
            while (process.bytesToWrite() > 0 && !stop && process.state() != QProcess::NotRunning)
                process.waitForBytesWritten(kPollMs);
            if (process.state() == QProcess::NotRunning) break;
            if (!stop) ++written;
        }
        if (!stop) process.closeWriteChannel();
        finishProcess(process, stop);
        if (!stop && !succeeded(process)) encodeError = lastError(process);
        rendered.close();   // an encoder that quit early must not leave the renderer waiting
        encoderDone = true;
    });

    DeliverResult result;
    qint64 lastWritten = -1;
    QElapsedTimer quiet;
    quiet.start();
    int lastPercent = -2;
    // Cancel, time out and stall checks, run between every wait of the drawing loop.
    auto interrupted = [&]() -> bool {
        const qint64 now = written;
        if (now != lastWritten) {
            lastWritten = now;
            quiet.restart();
            const int percent = job.expectedFrames > 0
                ? int(std::clamp<qint64>(now * 100 / job.expectedFrames, 0, 99)) : -1;
            if (job.progress && percent != lastPercent) job.progress(percent);
            lastPercent = percent;
        }
        if (job.cancelled && job.cancelled()) result.error = QStringLiteral("video export cancelled");
        else if (job.timeoutMs >= 0 && elapsed.elapsed() >= job.timeoutMs)
            result.error = QStringLiteral("ffmpeg export timed out");
        else if (job.stallTimeoutMs >= 0 && quiet.elapsed() >= job.stallTimeoutMs)
            result.error = QStringLiteral("ffmpeg stopped making progress");
        return !result.error.isEmpty();
    };

    qint64 index = 0;
    bool aborted = false;
    while (!aborted) {
        if ((aborted = interrupted())) break;
        std::optional<QImage> source = decoded.pop(kPollMs);
        if (!source) continue;
        if (source->isNull()) break;
        QImage output(job.outputSize, QImage::Format_RGB32);
        job.render(index++, *source, output);
        FrameQueue::Push pushed;
        while ((pushed = rendered.push(output, kPollMs)) == FrameQueue::Push::Full)
            if ((aborted = interrupted())) break;
        if (pushed == FrameQueue::Push::Closed) break;
    }
    rendered.finish();
    while (!aborted && !encoderDone) {
        if ((aborted = interrupted())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (aborted) {
        stop = true;
        decoded.close();
        rendered.close();
    }
    decoder.join();
    encoder.join();
    if (aborted) return result;
    interrupted();   // the last progress report
    result.error.clear();
    if (!encodeError.isEmpty()) result.error = encodeError;
    else if (!decodeError.isEmpty()) result.error = decodeError;
    else if (index == 0) result.error = QStringLiteral("ffmpeg decoded no frames");
    else if (written < index) result.error = QStringLiteral("ffmpeg stopped before the last frame");
    result.ok = result.error.isEmpty();
    return result;
}

}
```

In `CMakeLists.txt` nach `    src/studiorenderer.cpp`: `    src/renderexport.cpp`.

- [x] **Step 4: Bestehen prüfen, dreimal** (Threads und Prozesse: ein Zufallstreffer reicht nicht)

Run: `cmake --build build-rel --parallel 2 --target test_renderexport && for i in 1 2 3; do ctest --test-dir build-rel -R '^test_renderexport$' --output-on-failure | tail -1; done`
Expected: dreimal `Total Test time`, davor jeweils `100% tests passed`.

- [x] **Step 5: Checkpoint** `git diff --check` ohne Ausgabe. Kein Commit.

---

### Task 3: Export umschalten, Vergleich mit dem Filtergraph

**Files:**
- Modify: `src/videoexporter.h`, `src/videoexporter.cpp`, `tests/test_videoexporter.cpp`

**Interfaces:**
- Consumes: `CameraPath`, `CameraFrame`, `TimeMap` (S0), `StudioRenderer` (Task 1), `RenderPipeline`/`runRenderPipeline` (Task 2), `ZoomSegment` (S0).
- Produces: `VideoExportRequest::zooms` (`QVector<ZoomSegment>`), `DeliverResult writeVideoRendered(const VideoExportRequest &)`. `writeVideoWithOverlay` nimmt bei nicht leeren `zooms` den Renderpfad. S2 füllt `zooms` aus dem Dokument.

- [x] **Step 1: Tests schreiben.** Drei Tests ans Ende von `TestVideoExporter` (Diff gegen den heutigen Stand):

```diff
--- /home/mt/projects/eddy/tests/test_videoexporter.cpp	2026-09-23 23:23:40.762231436 +0200
+++ /tmp/eddy-s1/tests/test_videoexporter.cpp	2026-09-24 01:34:45.887317152 +0200
@@ -511,6 +511,136 @@
             QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")
         }), "trimmed output cannot be decoded completely");
     }
+    void renderedExportMatchesTheFilterGraph() {
+        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
+            QSKIP("ffmpeg/ffprobe not available");
+        QTemporaryDir dir;
+        QVERIFY(dir.isValid());
+        const QString input = dir.filePath(QStringLiteral("input.mp4"));
+        // Red left half, blue right half: the blur below straddles the edge.
+        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
+            "color=c=red:s=160x240:d=1:r=10", "-f", "lavfi", "-i", "color=c=blue:s=160x240:d=1:r=10",
+            "-filter_complex", "[0:v][1:v]hstack", "-pix_fmt", "yuv420p", input}));
+        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
+        overlay.fill(Qt::transparent);
+        QPainter(&overlay).fillRect(QRect(40, 40, 40, 40), Qt::green);
+        VideoExportRequest request{input, dir.filePath(QStringLiteral("graph.mp4")), overlay};
+        request.cropRect = QRect(20, 20, 280, 200);
+        request.blurRects = {QRect(140, 100, 40, 40)};
+        request.studio.background = StudioStyle::Background::Color;
+        request.studio.color = QColor(255, 255, 0);
+        request.studio.padding = 10;
+        request.studio.radius = 10;
+        request.studio.shadow = 0;
+        const DeliverResult graph = writeVideoWithOverlay(request);
+        QVERIFY2(graph.ok, qPrintable(graph.error));
+        const QString graphPath = request.outputPath;
+        request.outputPath = dir.filePath(QStringLiteral("rendered.mp4"));
+        const DeliverResult rendered = writeVideoRendered(request);
+        QVERIFY2(rendered.ok, qPrintable(rendered.error));
+
+        const auto a = probeVideoFile(graphPath), b = probeVideoFile(request.outputPath);
+        QVERIFY(a.ok && b.ok);
+        QCOMPARE(b.info.size, a.info.size);
+        QVERIFY2(qAbs(a.info.durationMs - b.info.durationMs) <= 100,
+                 qPrintable(QStringLiteral("%1 vs %2 ms").arg(a.info.durationMs).arg(b.info.durationMs)));
+        QCOMPARE(qRound(b.info.fps), 60);
+        auto frameAt = [&](const QString &video, const QString &name) {
+            const QString png = dir.filePath(name);
+            return runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", video,
+                                                         "-frames:v", "1", png}) ? QImage(png) : QImage();
+        };
+        const QImage fromGraph = frameAt(graphPath, QStringLiteral("graph.png"));
+        const QImage fromRender = frameAt(request.outputPath, QStringLiteral("rendered.png"));
+        QVERIFY(!fromGraph.isNull() && fromGraph.size() == fromRender.size());
+        const QPoint origin = studioLayout(QSize(280, 200), request.studio).content.topLeft();
+        // Background, red, blue, the annotation and the middle of the blur.
+        for (const QPoint &p : {QPoint(4, 4), origin + QPoint(60, 150), origin + QPoint(220, 150),
+                                origin + QPoint(40, 40), origin + QPoint(140, 100)}) {
+            const QColor g = fromGraph.pixelColor(p), r = fromRender.pixelColor(p);
+            QVERIFY2(qAbs(g.red() - r.red()) <= 10 && qAbs(g.green() - r.green()) <= 10
+                         && qAbs(g.blue() - r.blue()) <= 10,
+                     qPrintable(QStringLiteral("%1,%2: %3 vs %4").arg(p.x()).arg(p.y())
+                                    .arg(g.name(), r.name())));
+        }
+        qint64 difference = 0;
+        for (int y = 0; y < fromGraph.height(); ++y)
+            for (int x = 0; x < fromGraph.width(); ++x) {
+                const QRgb g = fromGraph.pixel(x, y), r = fromRender.pixel(x, y);
+                difference += qAbs(qRed(g) - qRed(r)) + qAbs(qGreen(g) - qGreen(r)) + qAbs(qBlue(g) - qBlue(r));
+            }
+        const double mean = double(difference) / (3.0 * fromGraph.width() * fromGraph.height());
+        QVERIFY2(mean < 3.0, qPrintable(QStringLiteral("mean difference %1").arg(mean)));
+    }
+
+    void zoomSegmentFillsTheOutputWithItsTarget() {
+        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
+            QSKIP("ffmpeg/ffprobe not available");
+        QTemporaryDir dir;
+        QVERIFY(dir.isValid());
+        QImage quadrants(320, 240, QImage::Format_RGB32);
+        {
+            QPainter p(&quadrants);
+            p.fillRect(0, 0, 160, 120, Qt::red);
+            p.fillRect(160, 0, 160, 120, Qt::green);
+            p.fillRect(0, 120, 160, 120, Qt::blue);
+            p.fillRect(160, 120, 160, 120, Qt::white);
+        }
+        const QString still = dir.filePath(QStringLiteral("quadrants.png"));
+        QVERIFY(quadrants.save(still));
+        const QString input = dir.filePath(QStringLiteral("input.mp4"));
+        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "1",
+            "-r", "30", "-vf", "setsar=1,format=yuv420p", input}));
+        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
+        overlay.fill(Qt::transparent);
+        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
+        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(240, 60),
+                          ZoomSegment::Motion::Instant}};
+        const DeliverResult result = writeVideoWithOverlay(request);
+        QVERIFY2(result.ok, qPrintable(result.error));
+        const auto probe = probeVideoFile(request.outputPath);
+        QVERIFY(probe.ok);
+        QCOMPARE(probe.info.size, QSize(320, 240));
+        QCOMPARE(qRound(probe.info.fps), 60);   // zooms went through the frame renderer
+        // Qt draws RGB; the output says which matrix turned it into YUV, so players do not guess.
+        QCOMPARE(processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams", "v:0",
+                 "-show_entries", "stream=color_space", "-of", "csv=p=0", request.outputPath}),
+                 QByteArray("bt709"));
+        const QString frame = dir.filePath(QStringLiteral("frame.png"));
+        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", request.outputPath,
+                                                     "-frames:v", "1", frame}));
+        const QImage decoded(frame);
+        for (const QPoint &p : {QPoint(20, 20), QPoint(300, 20), QPoint(20, 220), QPoint(300, 220), QPoint(160, 120)}) {
+            const QColor c = decoded.pixelColor(p);
+            QVERIFY2(c.green() > 200 && c.red() < 60 && c.blue() < 60,
+                     qPrintable(QStringLiteral("%1,%2 is %3").arg(p.x()).arg(p.y()).arg(c.name())));
+        }
+    }
+
+    void renderedExportKeepsTrimAndAudio() {
+        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
+            QSKIP("ffmpeg/ffprobe not available");
+        QTemporaryDir dir;
+        QVERIFY(dir.isValid());
+        const QString input = dir.filePath(QStringLiteral("input.mp4"));
+        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
+            "color=c=black:s=64x48:d=2:r=25", "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
+            "-shortest", "-pix_fmt", "yuv420p", input}));
+        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
+        overlay.fill(Qt::transparent);
+        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay, 500, 1500};
+        request.zooms = {{1, 0, 2000, 1.5, ZoomSegment::Target::Point, QPointF(32, 24),
+                          ZoomSegment::Motion::Focused}};
+        const DeliverResult result = writeVideoWithOverlay(request);
+        QVERIFY2(result.ok, qPrintable(result.error));
+        const auto probe = probeVideoFile(request.outputPath);
+        QVERIFY2(probe.ok, qPrintable(probe.error));
+        QVERIFY2(qAbs(probe.info.durationMs - 1000) <= 80,
+                 qPrintable(QStringLiteral("duration was %1 ms").arg(probe.info.durationMs)));
+        const QByteArray audio = processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams",
+            "a:0", "-show_entries", "stream=index", "-of", "csv=p=0", request.outputPath});
+        QVERIFY2(!audio.isEmpty(), "the rendered export lost its audio stream");
+    }
 };

 QTEST_GUILESS_MAIN(TestVideoExporter)
```

- [x] **Step 2: Scheitern prüfen**

Run: `cmake --build build-rel --parallel 2 --target test_videoexporter`
Expected: Build-Fehler, `VideoExportRequest` hat kein Mitglied `zooms` bzw. `writeVideoRendered` ist nicht deklariert.

- [x] **Step 3: Header**

```diff
--- /home/mt/projects/eddy/src/videoexporter.h	2026-09-23 21:28:41.671030586 +0200
+++ /tmp/eddy-s1/src/videoexporter.h	2026-09-24 01:31:50.286194102 +0200
@@ -5,6 +5,7 @@
 #include <QVector>
 #include <functional>
 #include "exporter.h"
+#include "studiodocument.h"
 #include "studiostyle.h"

 namespace eddy {
@@ -22,6 +23,9 @@
     QVector<QRect> blurRects;
     QRect cropRect;
     StudioStyle studio;   // framing around the (cropped) video; off by default
+    // Zoom segments in source time. Any at all send the export through the
+    // frame renderer (renderexport.h) instead of the filter graph.
+    QVector<ZoomSegment> zooms;
     // Called from the export thread with 0-99 as encoding advances, or -1
     // while the output length is unknown.
     std::function<void(int percent)> progress;
@@ -35,5 +39,8 @@
 // Re-encodes the source video with a static transparent overlay. Audio is kept
 // for mp4/mov/mkv outputs when ffmpeg can stream-copy it.
 DeliverResult writeVideoWithOverlay(const VideoExportRequest &req);
+// The same export, always drawn frame by frame at 60 fps; used for zooms and
+// by tests comparing both paths.
+DeliverResult writeVideoRendered(const VideoExportRequest &req);

 }
```

- [x] **Step 4: Exporter.** Gemeinsame Helfer herausziehen (`appendBlurFilters`, `encoderCodecArgs`,
`finishOutput`; der Filtergraph-Pfad nutzt sie ohne Verhaltensänderung), `writeRendered` ergänzen,
`writeVideoWithOverlay` wird zu `writeVideo(req, render)` mit zwei öffentlichen Einstiegen. Im
Renderpfad entfallen die temporären Overlay- und Studio-PNGs.

```diff
--- /home/mt/projects/eddy/src/videoexporter.cpp	2026-09-23 23:36:24.436016698 +0200
+++ /tmp/eddy-s1/src/videoexporter.cpp	2026-09-24 01:35:32.471909181 +0200
@@ -1,6 +1,10 @@
 #include "videoexporter.h"
 #include "exporter.h"
 #include "mediaio.h"
+#include "camerapath.h"
+#include "renderexport.h"
+#include "studiorenderer.h"
+#include "timemap.h"
 #include <QDir>
 #include <QFile>
 #include <QFileInfo>
@@ -61,6 +65,58 @@
     return false;
 }

+// Blurred patches over `current`, each a boxblurred crop overlaid in place.
+// Returns the label of the result.
+static QString appendBlurFilters(QString &filter, QString current, const QVector<QRect> &rects, QSize bounds) {
+    constexpr int videoBlurRadius = 12;
+    int blurIndex = 0;
+    for (const QRect &requested : rects) {
+        const QRect rect = requested.intersected(QRect(QPoint(), bounds));
+        if (rect.isEmpty()) continue;
+        const QString base = QStringLiteral("[blurbase%1]").arg(blurIndex);
+        const QString crop = QStringLiteral("[blurcrop%1]").arg(blurIndex);
+        const QString blurred = QStringLiteral("[blurpatch%1]").arg(blurIndex);
+        const QString next = QStringLiteral("[blurvideo%1]").arg(blurIndex);
+        filter += current + QStringLiteral("split=2") + base + crop + QStringLiteral(";");
+        filter += crop + QStringLiteral(
+            "crop=%1:%2:%3:%4,boxblur="
+            "luma_radius=min(%5\\,(min(w\\,h)-1)/2):luma_power=2:"
+            "chroma_radius=min(%5\\,(min(cw\\,ch)-1)/2):chroma_power=2")
+            .arg(rect.width()).arg(rect.height()).arg(rect.x()).arg(rect.y())
+            .arg(videoBlurRadius) + blurred + QStringLiteral(";");
+        filter += base + blurred + QStringLiteral("overlay=%1:%2:format=auto")
+            .arg(rect.x()).arg(rect.y()) + next + QStringLiteral(";");
+        current = next;
+        ++blurIndex;
+    }
+    return current;
+}
+
+// Codec arguments for `encoder`; empty means the CPU arguments as given.
+static QStringList encoderCodecArgs(const QString &encoder, const QStringList &cpuArgs, bool trimmed) {
+    if (encoder.isEmpty()) return cpuArgs;
+    QStringList codecs = {"-c:v", encoder, "-bf", "0",
+        encoder.endsWith("_vaapi") ? "-global_quality" : "-qp", "18",
+        "-c:a", trimmed ? "aac" : "copy", "-movflags", "+faststart"};
+    if (deviceArgs(encoder).isEmpty()) codecs += {"-pix_fmt", "yuv420p"};
+    if (trimmed) codecs += {"-b:a", "192k"};
+    return codecs;
+}
+
+// The rename that puts an output written next to its own input in place.
+static DeliverResult finishOutput(bool replaceInput, const QString &written, const QString &output) {
+    if (replaceInput) {
+        auto renamed = replaceFileAtomically(written, output);
+        if (!renamed.ok) {
+            QFile::remove(written);
+            return renamed;
+        }
+    }
+    DeliverResult r;
+    r.ok = true;
+    return r;
+}
+
 static QString sameDirTempTemplate(const QString &outputPath) {
     const QFileInfo out(outputPath);
     const QString suffix = out.suffix().isEmpty() ? QStringLiteral("mp4") : out.suffix();
@@ -108,7 +164,73 @@
     return r;
 }

-DeliverResult writeVideoWithOverlay(const VideoExportRequest &req) {
+// The frame-rendered export: ffmpeg decodes (orientation, sample aspect, blur,
+// 60 fps), Qt draws the camera's view and the Studio frame, ffmpeg encodes and
+// takes the audio straight from the source (studio plan 4.2).
+static DeliverResult writeRendered(const VideoExportRequest &req, const QString &ffmpeg,
+                                   const QString &output, const QStringList &cpuCodecs,
+                                   const VideoInfo &source, bool overlayVisible,
+                                   const QElapsedTimer &elapsed) {
+    const bool trimmed = req.trimOutMs >= 0;
+    const qint64 endMs = trimmed ? req.trimOutMs : source.durationMs;
+    const QRect content = req.cropRect.isNull() ? req.overlay.rect() : req.cropRect;
+    const CameraPath camera(req.zooms, TimeMap(source.durationMs, req.trimInMs, endMs, {}),
+                            CameraFrame{QRectF(content), 0, {}});
+    const StudioRenderer renderer(req.overlay.size(), content, req.studio,
+                                  overlayVisible ? req.overlay : QImage());
+    const QSize size = renderer.outputSize();
+    QStringList seek;
+    if (req.trimInMs > 0) seek = {"-ss", QString::number(req.trimInMs / 1000.0, 'f', 3)};
+    QStringList length;
+    if (trimmed) length = {"-t", QString::number((req.trimOutMs - req.trimInMs) / 1000.0, 'f', 3)};
+
+    QString filter = QStringLiteral("[0:v]fps=60,scale=%1:%2,setsar=1[base];")
+        .arg(req.overlay.width()).arg(req.overlay.height());
+    const QString blurred = appendBlurFilters(filter, QStringLiteral("[base]"), req.blurRects,
+                                              req.overlay.size());
+    filter += blurred + QStringLiteral("format=bgra[v]");
+    RenderPipeline job;
+    job.ffmpeg = ffmpeg;
+    job.sourceSize = req.overlay.size();
+    job.decodeArgs = QStringList{"-hide_banner", "-loglevel", "error", "-nostdin"} + seek
+        + QStringList{"-i", req.inputPath} + length
+        + QStringList{"-filter_complex", filter, "-map", "[v]", "-f", "rawvideo", "-pix_fmt", "bgra", "-"};
+    job.outputSize = size;
+    job.expectedFrames = qRound64((endMs - req.trimInMs) * 60 / 1000.0);
+    job.render = [&](qint64 index, const QImage &frame, QImage &out) {
+        renderer.render(frame, camera.rectAt(index * 1000.0 / 60), out);
+    };
+    job.stallTimeoutMs = req.stallTimeoutMs;
+    job.progress = req.progress;
+    job.cancelled = req.cancelled;
+
+    const QString ext = QFileInfo(req.outputPath).suffix().toLower();
+    const QString hardware = ext != "webm" && (req.timeoutMs < 0 || req.timeoutMs >= 5000)
+        ? hardwareEncoder(ffmpeg) : QString();
+    QStringList encoders;
+    if (!hardware.isEmpty()) encoders.append(hardware);
+    encoders.append(QString());   // the CPU encoder also takes what the hardware one refuses
+    DeliverResult r;
+    for (const QString &encoder : encoders) {
+        const QStringList device = deviceArgs(encoder);
+        // Qt draws RGB; the BT.709 matrix also tags the output, so players do not guess.
+        job.encodeArgs = QStringList{"-hide_banner", "-loglevel", "error", "-y"} + device
+            + QStringList{"-f", "rawvideo", "-pix_fmt", "bgra", "-s",
+                          QStringLiteral("%1x%2").arg(size.width()).arg(size.height()), "-r", "60", "-i", "-"}
+            + seek + length + QStringList{"-i", req.inputPath, "-map", "0:v", "-map", "1:a?",
+                "-vf", QStringLiteral("scale=out_color_matrix=bt709:out_range=tv,format=")
+                           + (device.isEmpty() ? QStringLiteral("yuv420p") : QStringLiteral("nv12,hwupload"))}
+            + encoderCodecArgs(encoder, cpuCodecs, trimmed) + QStringList{"-shortest", output};
+        job.timeoutMs = req.timeoutMs < 0 ? -1 : qMax(0, req.timeoutMs - int(elapsed.elapsed()));
+        r = runRenderPipeline(job);
+        if (r.ok || r.error.contains(QStringLiteral("cancelled")) || r.error.contains(QStringLiteral("timed out")))
+            return r;
+        if (!encoder.isEmpty()) r.error += QStringLiteral(" with ") + encoder;
+    }
+    return r;
+}
+
+static DeliverResult writeVideo(const VideoExportRequest &req, bool render) {
     QElapsedTimer elapsed;
     elapsed.start();
     DeliverResult r;
@@ -144,7 +266,7 @@

     const bool overlayVisible = hasVisiblePixels(req.overlay);
     QTemporaryFile overlayTmp(QDir::tempPath() + QStringLiteral("/eddy-overlay-XXXXXX.png"));
-    if (overlayVisible) {
+    if (overlayVisible && !render) {
         if (!overlayTmp.open()) {
             r.error = QStringLiteral("cannot create temporary overlay");
             return r;
@@ -164,7 +286,7 @@
     const StudioLayout studio = studioLayout(contentSize, req.studio);
     QTemporaryFile studioBackground(QDir::tempPath() + QStringLiteral("/eddy-studio-bg-XXXXXX.png"));
     QTemporaryFile studioMask(QDir::tempPath() + QStringLiteral("/eddy-studio-mask-XXXXXX.png"));
-    if (req.studio.active()) {
+    if (req.studio.active() && !render) {
         const std::pair<QTemporaryFile *, QImage> stills[] = {
             {&studioBackground, renderStudioBackground(contentSize, req.studio)},
             {&studioMask, renderStudioFrameMask(contentSize, req.studio)}};
@@ -218,7 +340,6 @@
             codecArgs += {QStringLiteral("-b:a"), QStringLiteral("192k")};
     }

-    constexpr int videoBlurRadius = 12;
     // ffmpeg autorotates before the filter graph. Normalize sample aspect ratio
     // to the same display-pixel coordinate system used by the editor.
     // Drop surplus frames before the filters: -fpsmax alone still scales,
@@ -227,30 +348,16 @@
     const bool highFps = source.fps > 60.0;
     const qint64 outputMs = trimmed ? req.trimOutMs - req.trimInMs
                                     : source.durationMs - req.trimInMs;
+    if (render) {
+        const DeliverResult rendered = writeRendered(req, ffmpeg, actualOutput, codecArgs, source,
+                                                     overlayVisible, elapsed);
+        return rendered.ok ? finishOutput(replaceInput, actualOutput, req.outputPath) : rendered;
+    }
     QString filter = QStringLiteral("[0:v]%1scale=%2:%3,setsar=1[base];")
         .arg(highFps ? QStringLiteral("fps=60,") : QString())
         .arg(req.overlay.width()).arg(req.overlay.height());
-    QString current = QStringLiteral("[base]");
-    int blurIndex = 0;
-    for (const QRect &requested : req.blurRects) {
-        const QRect rect = requested.intersected(req.overlay.rect());
-        if (rect.isEmpty()) continue;
-        const QString base = QStringLiteral("[blurbase%1]").arg(blurIndex);
-        const QString crop = QStringLiteral("[blurcrop%1]").arg(blurIndex);
-        const QString blurred = QStringLiteral("[blurpatch%1]").arg(blurIndex);
-        const QString next = QStringLiteral("[blurvideo%1]").arg(blurIndex);
-        filter += current + QStringLiteral("split=2") + base + crop + QStringLiteral(";");
-        filter += crop + QStringLiteral(
-            "crop=%1:%2:%3:%4,boxblur="
-            "luma_radius=min(%5\\,(min(w\\,h)-1)/2):luma_power=2:"
-            "chroma_radius=min(%5\\,(min(cw\\,ch)-1)/2):chroma_power=2")
-            .arg(rect.width()).arg(rect.height()).arg(rect.x()).arg(rect.y())
-            .arg(videoBlurRadius) + blurred + QStringLiteral(";");
-        filter += base + blurred + QStringLiteral("overlay=%1:%2:format=auto")
-            .arg(rect.x()).arg(rect.y()) + next + QStringLiteral(";");
-        current = next;
-        ++blurIndex;
-    }
+    const QString current = appendBlurFilters(filter, QStringLiteral("[base]"), req.blurRects,
+                                              req.overlay.size());
     filter += current + (overlayVisible ? QStringLiteral("[1:v]overlay=0:0:format=auto:shortest=1")
                                        : QStringLiteral("null"));
     if (!crop.isNull())
@@ -321,14 +428,7 @@
             return r;
         }
         const QStringList device = deviceArgs(encoder);
-        QStringList codecs = codecArgs;
-        if (!encoder.isEmpty()) {
-            codecs = {"-c:v", encoder, "-bf", "0",
-                encoder.endsWith("_vaapi") ? "-global_quality" : "-qp", "18",
-                "-c:a", trimmed ? "aac" : "copy", "-movflags", "+faststart"};
-            if (device.isEmpty()) codecs += {"-pix_fmt", "yuv420p"};
-            if (trimmed) codecs += {"-b:a", "192k"};
-        }
+        const QStringList codecs = encoderCodecArgs(encoder, codecArgs, trimmed);
         QProcess p;
         p.start(ffmpeg, device + inputs + QStringList{"-filter_complex", filter
             + (device.isEmpty() ? QString() : QStringLiteral(",format=nv12,hwupload"))
@@ -391,17 +491,15 @@
         if (r.error.isEmpty()) r.error = QStringLiteral("ffmpeg failed");
     }
     if (!r.error.isEmpty()) return r;
+    return finishOutput(replaceInput, actualOutput, req.outputPath);
+}

-    if (replaceInput) {
-        auto renamed = replaceFileAtomically(actualOutput, req.outputPath);
-        if (!renamed.ok) {
-            QFile::remove(actualOutput);
-            return renamed;
-        }
-    }
+DeliverResult writeVideoWithOverlay(const VideoExportRequest &req) {
+    return writeVideo(req, !req.zooms.isEmpty());
+}

-    r.ok = true;
-    return r;
+DeliverResult writeVideoRendered(const VideoExportRequest &req) {
+    return writeVideo(req, true);
 }

 }
```

- [x] **Step 5: Bestehen prüfen**

Run: `cmake --build build-rel --parallel 2 --target test_videoexporter && ctest --test-dir build-rel -R '^test_videoexporter$' --output-on-failure`
Expected: `100% tests passed` (21 Testfunktionen inklusive der drei neuen).

- [x] **Step 6: Gesamtlauf**

Run: `cmake --build build-rel --parallel 2; echo build=$?; ctest --test-dir build-rel | tail -3; git diff --check`
Expected: `build=0`, `100% tests passed, 0 tests failed out of 33`, keine Ausgabe von `diff --check`.
Hängt `test_crop` wieder im Watchdog: erst Backtrace per gdb, dann weiter.

- [x] **Step 7: Messen und ansehen.** Mit einem Wegwerf-Programm gegen `libeddy_core.a`
(`/tmp`, nicht im Repo) den echten Clip exportieren: 5 s ab Sekunde 5, Dusk-Rahmen, einmal ohne
und einmal mit einem 2×-Focused-Zoom von 6 bis 9 s. Zeiten notieren, ein Frame vor und eines im
Segment ansehen. Erwartet: Renderpfad mindestens Echtzeit (unter 5 s), Bild wie oben beschrieben.

- [x] **Step 8: Plan-Stand nachtragen** in `docs/plans/2026-09-24-studio-features.md`,
Abschnitt 9, unter "Umsetzungsstand": S1 umgesetzt, gemessene Zeiten.

- [x] **Step 9: Checkpoint** `git status --short`: neu nur die vier Quell- und zwei Testdateien plus
dieser Plan; geändert `CMakeLists.txt`, `src/videoexporter.*`, `tests/test_videoexporter.cpp`,
der Studio-Plan. Kein Commit.

---

## Abdeckung gegen den Gesamtplan

| Anforderung | Task |
| --- | --- |
| 2: überlappende Pipeline, QPainter statt QRhi | 2 |
| 4.1: Umschalten nur bei zeitabhängigem Inhalt (Zooms) | 3 |
| 4.2.1: Decoder mit Orientierung, SAR, Blur, 60 fps, rawvideo bgra | 3 |
| 4.2.2: Kamera aus dem Cache, Rahmen, Annotationen in eigener Auflösung | 1, 3 |
| 4.2.3: Encoder, Ton aus der Quelle, Hardware zuerst, CPU-Rückfall | 3 |
| 4.2.4: Warteschlangen nach Bytes begrenzt | 2 |
| 4.2.5: Fortschritt, Stall, Abbrechen, Timeout | 2 |
| Referenztest gegen `maskedmerge` | 3 |

Bewusst später: hochaufgelöstes Annotations-Overlay aus der Szene (S2, der Renderer nimmt es
schon in jeder Auflösung), Fragmente im Decoder (S4), GIF (S3).
