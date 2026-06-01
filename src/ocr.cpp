#include "ocr.h"
#include <algorithm>
#include <map>
#include <tuple>

namespace eddy::ocr {

namespace {

int edgeRight(const QRect &r)  { return r.x() + r.width(); }
int edgeBottom(const QRect &r) { return r.y() + r.height(); }

bool rectsIntersect(const QRect &a, const QRect &b) {
    return a.x() < edgeRight(b) && edgeRight(a) > b.x()
        && a.y() < edgeBottom(b) && edgeBottom(a) > b.y();
}

QRect unionRect(const QRect &a, const QRect &b) {
    const int x = qMin(a.x(), b.x());
    const int y = qMin(a.y(), b.y());
    const int right = qMax(edgeRight(a), edgeRight(b));
    const int bottom = qMax(edgeBottom(a), edgeBottom(b));
    return QRect(x, y, right - x, bottom - y);
}

QRect padRect(const QRect &r, int pad) {
    if (pad < 0) pad = 0;
    return QRect(r.x() - pad, r.y() - pad, r.width() + 2 * pad, r.height() + 2 * pad);
}

bool validateTsvHeader(const QString &headerIn) {
    static const QStringList expected = {
        "level", "page_num", "block_num", "par_num", "line_num", "word_num",
        "left", "top", "width", "height", "conf", "text"};
    QString h = headerIn;
    if (h.endsWith(QLatin1Char('\r'))) h.chop(1);
    return h.split(QLatin1Char('\t')) == expected;
}

// Split into at most `maxFields`: the final field keeps any remaining tabs
// (mirrors snaptext's splitn(12, '\t')).
QStringList splitTsvRow(const QString &row, int maxFields) {
    QStringList out;
    int start = 0;
    for (int i = 0; i < maxFields - 1; ++i) {
        const int tab = row.indexOf(QLatin1Char('\t'), start);
        if (tab < 0) { out.append(row.mid(start)); return out; }
        out.append(row.mid(start, tab - start));
        start = tab + 1;
    }
    out.append(row.mid(start));
    return out;
}

} // namespace

QVector<const OcrWord *> OcrDocument::wordsIntersecting(const QRect &region) const {
    QVector<const OcrWord *> out;
    for (const OcrWord &w : words)
        if (rectsIntersect(w.rect, region)) out.append(&w);
    return out;
}

QVector<QRect> OcrDocument::textRegionsIntersecting(const QRect &region,
                                                    int padding, int mergeLineGap) const {
    QVector<QRect> regions;
    for (const OcrLine &ln : lines)
        if (rectsIntersect(ln.rect, region))
            regions.append(padRect(ln.rect, padding));

    std::sort(regions.begin(), regions.end(), [](const QRect &a, const QRect &b) {
        if (a.y() != b.y()) return a.y() < b.y();
        return a.x() < b.x();
    });

    QVector<QRect> merged;
    for (const QRect &rc : regions) {
        if (merged.isEmpty()) { merged.append(rc); continue; }
        QRect &last = merged.last();
        const int verticalGap = rc.y() - edgeBottom(last);
        const bool horizontalOverlap = rc.x() < edgeRight(last) && edgeRight(rc) > last.x();
        if (verticalGap <= mergeLineGap && horizontalOverlap)
            last = unionRect(last, rc);
        else
            merged.append(rc);
    }
    return merged;
}

bool parseTesseractTsv(const QString &tsv, OcrDocument *out, QString *error) {
    auto fail = [&](const QString &m) { if (error) *error = m; return false; };
    OcrDocument doc;

    const QStringList rows = tsv.split(QLatin1Char('\n'));
    const bool empty = rows.isEmpty() || (rows.size() == 1 && rows.first().isEmpty());
    if (empty) { if (out) *out = doc; return true; }

    if (!validateTsvHeader(rows.first()))
        return fail(QStringLiteral("unexpected tesseract TSV header"));

    std::map<std::tuple<int, int, int, int>, QVector<int>> lineWords;

    for (int r = 1; r < rows.size(); ++r) {
        QString row = rows.at(r);
        if (row.endsWith(QLatin1Char('\r'))) row.chop(1);
        if (row.trimmed().isEmpty()) continue;

        const QStringList f = splitTsvRow(row, 12);
        if (f.size() != 12)
            return fail(QStringLiteral("invalid TSV row %1: expected 12 fields").arg(r + 1));

        bool levelOk = false;
        const int level = f.at(0).toInt(&levelOk);
        if (!levelOk) return fail(QStringLiteral("invalid level at TSV row %1").arg(r + 1));
        if (level != 5) continue;

        const QString text = f.at(11).trimmed();
        if (text.isEmpty()) continue;

        bool intsOk = true;
        auto pint = [&](int col) { bool g = false; int v = f.at(col).toInt(&g); if (!g) intsOk = false; return v; };
        const int page = pint(1), block = pint(2), par = pint(3), line = pint(4);
        const int left = pint(6), top = pint(7), width = pint(8), height = pint(9);
        if (!intsOk) return fail(QStringLiteral("invalid integer field at TSV row %1").arg(r + 1));

        float conf = -1.0f; bool hasConf = false;
        const QString confStr = f.at(10);
        if (confStr != QLatin1String("-1")) {
            bool g = false; conf = confStr.toFloat(&g);
            if (!g) return fail(QStringLiteral("invalid conf at TSV row %1").arg(r + 1));
            hasConf = true;
        }

        OcrWord w;
        w.text = text;
        w.rect = QRect(left, top, width, height);
        w.confidence = conf;
        w.hasConfidence = hasConf;
        const int wi = doc.words.size();
        doc.words.append(w);
        lineWords[std::make_tuple(page, block, par, line)].append(wi);
    }

    for (const auto &kv : lineWords) {
        const QVector<int> &indices = kv.second;
        if (indices.isEmpty()) continue;
        QRect rect = doc.words.at(indices.first()).rect;
        QStringList parts;
        parts.reserve(indices.size());
        for (int i = 0; i < indices.size(); ++i) {
            const OcrWord &w = doc.words.at(indices.at(i));
            rect = (i == 0) ? w.rect : unionRect(rect, w.rect);
            parts.append(w.text);
        }
        OcrLine ln;
        ln.text = parts.join(QLatin1Char(' '));
        ln.rect = rect;
        ln.wordIndices = indices;
        doc.lines.append(ln);
    }

    QStringList lineTexts;
    lineTexts.reserve(doc.lines.size());
    for (const OcrLine &ln : doc.lines) lineTexts.append(ln.text);
    doc.text = lineTexts.join(QLatin1Char('\n'));

    if (out) *out = doc;
    return true;
}

OcrDocument mapToSourceCoords(OcrDocument doc, const QPoint &, int) { return doc; }

int chooseScale(const QSize &) { return 1; }

QString chooseTessdataDir(const QString &, const QString &, const QString &,
                          const QString &, const std::function<bool(const QString &)> &) {
    return {};
}

OcrRunner::OcrRunner(QObject *parent) : QObject(parent) {}
OcrRunner::~OcrRunner() { cleanup(); }
void OcrRunner::recognizeRegion(const QImage &, const QRect &, const OcrOptions &) {}
void OcrRunner::cleanup() {}

} // namespace eddy::ocr
