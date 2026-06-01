#include "redactocrcontroller.h"
#include "items/redactitem.h"
#include <utility>

namespace eddy {

RedactOcrController::RedactOcrController(QImage background, ocr::OcrOptions options, QObject *parent)
    : QObject(parent), m_bg(std::move(background)), m_opts(std::move(options)) {
    connect(&m_runner, &ocr::OcrRunner::finished, this, [this](const ocr::OcrDocument &doc) {
        RedactItem *item = m_target;
        m_target = nullptr;
        if (!item) return;                    // forgotten/cancelled while in flight
        if (!applyResult(item, doc)) emit noTextDetected();
    });
    connect(&m_runner, &ocr::OcrRunner::failed, this, [this](const QString &msg) {
        RedactItem *item = m_target;
        m_target = nullptr;
        if (item) item->setDetecting(false);
        emit ocrFailed(msg);
    });
}

void RedactOcrController::detectFor(RedactItem *item) {
    if (!item) return;
    if (m_target && m_target != item) {
        // Un-stick the previous target only if it already has results to show; an OCR
        // target with no results must stay covered (detecting) — never expose source.
        if (!m_target->textRects().isEmpty()) m_target->setDetecting(false);
    }
    m_target = item;
    item->setDetecting(true);
    // recognizeRegion() may emit failed() SYNCHRONOUSLY (e.g. an empty/off-image crop),
    // re-entering our failed handler — do not touch m_target after this call.
    m_runner.recognizeRegion(m_bg, item->mapRectToScene(item->rect()).toAlignedRect(), m_opts);
}

void RedactOcrController::forget(const RedactItem *item) {
    if (m_target == item) m_target = nullptr;
}

void RedactOcrController::cancel() {
    m_target = nullptr;   // OcrRunner is single-flight; the next detectFor cancels its process
}

bool RedactOcrController::applyResult(RedactItem *item, const ocr::OcrDocument &doc) const {
    const QRect region = item->mapRectToScene(item->rect()).toAlignedRect();
    const QVector<QRect> rects = doc.textRegionsIntersecting(region);
    QVector<QRectF> frects;
    frects.reserve(rects.size());
    for (const QRect &r : rects) frects.append(item->mapRectFromScene(QRectF(r)));   // scene -> item-local
    item->setTextRects(frects);
    item->setDetecting(false);
    return !frects.isEmpty();
}

}
