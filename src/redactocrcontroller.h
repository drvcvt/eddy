#pragma once
#include <QObject>
#include <QImage>
#include "ocr.h"

namespace eddy {

class RedactItem;

// Drives OCR text-detection for redact items. Owns one OcrRunner (single-flight):
// detectFor() marks the item detecting and runs tesseract over the background for
// the item's region; on completion it narrows the item's text rects (or, if no text,
// leaves them empty and emits noTextDetected()). The UI (Phase 3b) calls detectFor()
// when a redact enters an OCR mode or is resized.
//
// Lifetime contract: the controller holds a raw RedactItem* as its current target
// (RedactItem is not a QObject). The owner MUST call forget(item) before deleting an
// item that may be the in-flight target (e.g. on RemoveItemCommand), so a late result
// is not applied to a dangling pointer.
class RedactOcrController : public QObject {
    Q_OBJECT
public:
    RedactOcrController(QImage background, ocr::OcrOptions options, QObject *parent = nullptr);

    void setBackground(const QImage &background) { m_bg = background; }
    void detectFor(RedactItem *item);
    void forget(const RedactItem *item);   // drop the in-flight result if it targets `item`
    void cancel();                         // forget any current target

    // Set `item`'s text rects from `doc` (lines intersecting the item's region,
    // merged), clear its detecting flag, and notify the export cache.
    bool applyResult(RedactItem *item, const ocr::OcrDocument &doc);

signals:
    void contentChanged();
    void noTextDetected();
    void ocrFailed(const QString &message);

private:
    ocr::OcrRunner m_runner;
    QImage m_bg;
    ocr::OcrOptions m_opts;
    RedactItem *m_target = nullptr;
};

}
