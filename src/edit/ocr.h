#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QRect>
#include <QPoint>
#include <QSize>
#include <QImage>
#include <QObject>
#include <functional>

class QProcess;

namespace eddy::ocr {

struct OcrWord {
    QString text;
    QRect rect;
    float confidence = -1.0f;
    bool hasConfidence = false;
};

struct OcrLine {
    QString text;
    QRect rect;
    QVector<int> wordIndices;
};

struct OcrDocument {
    QString text;
    QVector<OcrLine> lines;
    QVector<OcrWord> words;

    // Words whose box overlaps `region` (half-open overlap). Pointers are valid
    // for the lifetime of this document (no further mutation).
    QVector<const OcrWord *> wordsIntersecting(const QRect &region) const;

    // Line boxes intersecting `region`, padded then merged (vertically adjacent
    // + horizontally overlapping) into cover rects. Ports snaptext
    // `text_regions_intersecting`.
    QVector<QRect> textRegionsIntersecting(const QRect &region,
                                           int padding = 4,
                                           int mergeLineGap = 8) const;
};

// Parse tesseract TSV (produced with `-c tessedit_create_tsv=1`). Returns false
// and sets *error on a malformed header/row; on success fills *out. `error` may
// be null. An empty input yields an empty document and returns true.
bool parseTesseractTsv(const QString &tsv, OcrDocument *out, QString *error = nullptr);

// Translate (+ optional downscale by `scale`) all rects from crop-image space
// back to source/scene space: divide by `scale` (ceil on the far edge), then
// offset by `cropOrigin`. `scale` is clamped to >= 1.
OcrDocument mapToSourceCoords(OcrDocument doc, const QPoint &cropOrigin, int scale);

// Upscale factor for a crop of the given size (2x for small crops to help OCR
// of small screenshot text; 1x for large crops to bound temp size).
int chooseScale(const QSize &cropSize);

// Resolve the tessdata directory. `exists` is injected for testability.
// Order: explicit -> envPrefix -> /usr/share/tessdata -> home/.local/share/tessdata.
// Returns an empty string when none has all requested languages.
QString chooseTessdataDir(const QString &explicitDir,
                          const QString &envPrefix,
                          const QString &home,
                          const QString &language,
                          const std::function<bool(const QString &)> &exists);

struct OcrOptions {
    QString language = QStringLiteral("deu");   // eng is NOT installed locally
    int psm = 6;
    QString tessdataDir;   // empty = auto-resolve
};

struct OcrRuntime {
    QString program;
    QString tessdataDir;
};

OcrRuntime resolveOcrRuntime(
    const OcrOptions &opts,
    const QString &applicationDir,
    const std::function<bool(const QString &)> &exists,
    const std::function<QString(const QString &)> &findExecutable);

// Async tesseract runner: crops `bg` to `region` (+small pad), upscales small
// crops, writes a temp PNG, runs tesseract, parses TSV, maps rects back to `bg`
// (scene) coordinates. Emits exactly one of finished()/failed() per call. One
// in-flight request at a time; a new call cancels the previous.
class OcrRunner : public QObject {
    Q_OBJECT
public:
    explicit OcrRunner(QObject *parent = nullptr);
    ~OcrRunner() override;
    void recognizeRegion(const QImage &bg, const QRect &region, const OcrOptions &opts = {});

signals:
    void finished(const eddy::ocr::OcrDocument &doc);   // rects in bg/scene coords
    void failed(const QString &message);

private:
    void cleanup();
    void reportFinished(const OcrDocument &doc);  // emits finished() at most once per request
    void reportFailed(const QString &message);    // emits failed() at most once per request
    bool m_settled = false;
    QProcess *m_proc = nullptr;
    QString m_tmpPath;
    int m_scale = 1;
    QPoint m_cropOrigin;
};

} // namespace eddy::ocr
