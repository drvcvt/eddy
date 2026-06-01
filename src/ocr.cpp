#include "ocr.h"
#include <algorithm>
#include <map>
#include <tuple>
#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

OcrDocument mapToSourceCoords(OcrDocument doc, const QPoint &cropOrigin, int scale) {
    if (scale < 1) scale = 1;
    // OCR crop coords are always non-negative, so floor division matches the reference's truncating divide.
    auto mapRect = [&](const QRect &r) {
        const int x = r.x() / scale;
        const int y = r.y() / scale;
        const int right = (edgeRight(r) + scale - 1) / scale;     // ceil far edge
        const int bottom = (edgeBottom(r) + scale - 1) / scale;
        return QRect(x + cropOrigin.x(), y + cropOrigin.y(), right - x, bottom - y);
    };
    for (OcrWord &w : doc.words) w.rect = mapRect(w.rect);
    for (OcrLine &ln : doc.lines) ln.rect = mapRect(ln.rect);
    return doc;
}

int chooseScale(const QSize &cropSize) {
    const int maxDim = qMax(cropSize.width(), cropSize.height());
    if (maxDim <= 0) return 1;
    return maxDim <= 1600 ? 2 : 1;
}

QString chooseTessdataDir(const QString &explicitDir, const QString &envPrefix,
                          const QString &home, const QString &language,
                          const std::function<bool(const QString &)> &exists) {
    // Explicit/env overrides are trusted without existence checks (caller's responsibility).
    if (!explicitDir.isEmpty()) return explicitDir;
    if (!envPrefix.isEmpty()) return envPrefix;

    QVector<QString> candidates;
    candidates.append(QStringLiteral("/usr/share/tessdata"));
    if (!home.isEmpty())
        candidates.append(home + QStringLiteral("/.local/share/tessdata"));

    const QStringList langs = language.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    auto hasAllLangs = [&](const QString &dir) {
        for (const QString &lang : langs)
            if (!exists(dir + QLatin1Char('/') + lang + QStringLiteral(".traineddata")))
                return false;
        return true;
    };

    for (const QString &c : candidates)
        if (hasAllLangs(c)) return c;
    return QString();
}

OcrRunner::OcrRunner(QObject *parent) : QObject(parent) {}

OcrRunner::~OcrRunner() { cleanup(); }

void OcrRunner::recognizeRegion(const QImage &bg, const QRect &region, const OcrOptions &opts) {
    cleanup();
    m_settled = false;

    const QRect padded(region.x() - 2, region.y() - 2, region.width() + 4, region.height() + 4);
    const QRect crop = padded.intersected(bg.rect());
    if (crop.width() <= 0 || crop.height() <= 0) {
        reportFailed(QStringLiteral("redact region is empty"));
        return;
    }

    m_cropOrigin = crop.topLeft();
    m_scale = chooseScale(crop.size());

    QImage cropImg = bg.copy(crop);
    if (m_scale > 1)
        cropImg = cropImg.scaled(cropImg.width() * m_scale, cropImg.height() * m_scale,
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/eddy-ocr-XXXXXX.png"));
    tmp.setAutoRemove(false);
    if (!tmp.open()) { reportFailed(QStringLiteral("cannot create temp image")); return; }
    m_tmpPath = tmp.fileName();
    tmp.close();
    if (!cropImg.save(m_tmpPath, "PNG")) {
        QFile::remove(m_tmpPath);
        m_tmpPath.clear();
        reportFailed(QStringLiteral("cannot write temp image"));
        return;
    }

    const QString tessdata = chooseTessdataDir(
        opts.tessdataDir, qEnvironmentVariable("TESSDATA_PREFIX"), QDir::homePath(),
        opts.language, [](const QString &p) { return QFileInfo::exists(p); });

    QStringList args;
    args << m_tmpPath << QStringLiteral("stdout")
         << QStringLiteral("-l") << opts.language
         << QStringLiteral("--psm") << QString::number(opts.psm)
         << QStringLiteral("-c") << QStringLiteral("tessedit_create_tsv=1");
    if (!tessdata.isEmpty())
        args << QStringLiteral("--tessdata-dir") << tessdata;

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        const QString msg = m_proc ? m_proc->errorString() : QStringLiteral("failed to start");
        cleanup();                                   // tear down before emitting (re-entrancy safe)
        reportFailed(QStringLiteral("tesseract: %1").arg(msg));
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && code == 0);
        const QString stderrText = ok ? QString()
                                      : QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
        const QString tsv = ok ? QString::fromUtf8(m_proc->readAllStandardOutput()) : QString();
        const QPoint cropOrigin = m_cropOrigin;
        const int scale = m_scale;
        cleanup();                                   // tear down before emitting (re-entrancy safe)
        if (!ok) {
            reportFailed(QStringLiteral("tesseract failed: %1").arg(stderrText));
            return;
        }
        OcrDocument doc; QString perr;
        if (!parseTesseractTsv(tsv, &doc, &perr)) {
            reportFailed(QStringLiteral("OCR parse: %1").arg(perr));
            return;
        }
        reportFinished(mapToSourceCoords(doc, cropOrigin, scale));
    });

    m_proc->start(QStringLiteral("tesseract"), args);
}

void OcrRunner::cleanup() {
    if (m_proc) {
        m_proc->disconnect(this);
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(100);
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    if (!m_tmpPath.isEmpty()) {
        QFile::remove(m_tmpPath);
        m_tmpPath.clear();
    }
}

void OcrRunner::reportFinished(const OcrDocument &doc) {
    if (m_settled) return;
    m_settled = true;
    emit finished(doc);
}

void OcrRunner::reportFailed(const QString &message) {
    if (m_settled) return;
    m_settled = true;
    emit failed(message);
}

} // namespace eddy::ocr
