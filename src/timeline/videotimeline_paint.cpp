#include "timeline/videotimeline.h"
#include "timeline/audiowaveform.h"
#include "app/theme.h"
#include "timeline/zoomlane.h"
#include "chrome/motionicon.h"
#include "video/fragments.h"
#include <QFontDatabase>
#include <QHashFunctions>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMenu>
#include <cmath>

namespace eddy {

// A grey envelope, the same up and down, with a brighter RMS core on a fixed
// dB scale. Stretches not read yet get a dotted line, never a flat "silence".
// Drawn once into a cache: the playhead repaints the timeline every frame.
void VideoTimeline::paintWaveform(QPainter &painter, qreal inX, qreal outX, const std::function<QColor(qreal)> &ink) {
    const QRectF lane = waveformRect();
    const qreal dpr = devicePixelRatioF();
    const size_t key = qHashMulti(0, lane.width(), lane.height(), m_viewStart, m_viewEnd, dpr, m_waveRevision,
                                  ink(0).rgba(), ink(1).rgba(), int(m_waveform->state()));
    if (m_waveCache.isNull() || key != m_waveKey) {
        m_waveKey = key;
        m_waveCache = QImage((lane.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        m_waveCache.setDevicePixelRatio(dpr);
        m_waveCache.fill(Qt::transparent);
        QPainter p(&m_waveCache);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(-lane.topLeft());
        p.setPen(Qt::NoPen);
        p.fillRect(lane, ink(0.07));
        if (m_waveform->state() == AudioWaveformProvider::State::Failed) {
            QFont label = font();
            label.setPixelSize(theme::kFsMicro);
            p.setFont(label);
            p.setPen(ink(0.45));
            p.drawText(lane, Qt::AlignCenter, tr("Waveform unavailable"));
        } else {
            const qreal middle = lane.center().y(), reach = lane.height() / 2 - 2;
            // In dB from -60 to 0: screen recordings sit far below full scale,
            // and a linear scale drew them as a flat line.
            auto level = [](float amplitude) {
                return amplitude <= 0.001f ? 0.0 : std::clamp((20 * std::log10(double(amplitude)) + 60) / 60, 0.0, 1.0);
            };
            const QColor envelope = ink(0.32), core = ink(0.5), pending = ink(0.2);
            const int left = int(std::floor(lane.left())), right = int(std::ceil(lane.right()));
            for (int x = left; x < right; ++x) {
                // A column at a cut reads its own piece only, never the sound that was cut.
                const qint64 from = timeForX(x);
                qint64 to = timeForX(x + 1);
                if (!m_fragments.isEmpty())
                    for (const TimePiece &piece : m_axis.pieces())
                        if (from >= piece.srcStart && from < piece.srcEnd) to = std::min<qint64>(to, qint64(piece.srcEnd));
                const auto s = m_waveform->summary(from, to);
                if (!s.known) {
                    if (x % 4 == 0) p.fillRect(QRectF(x, middle - 0.5, 2, 1), pending);
                    continue;
                }
                const qreal peak = std::max<qreal>(0.5, reach * level(s.peak)), rms = reach * level(s.rms);
                p.fillRect(QRectF(x, middle - peak, 1, 2 * peak), envelope);
                if (rms >= 0.5) p.fillRect(QRectF(x, middle - rms, 1, 2 * rms), core);
            }
        }
    }
    QPainterPath shape;
    shape.addRoundedRect(lane, 6, 6);
    painter.save();
    painter.setClipPath(shape);
    painter.drawImage(lane.topLeft(), m_waveCache);
    QColor shade = palette().color(QPalette::Window);
    shade.setAlpha(205);
    painter.fillRect(QRectF(lane.left(), lane.top(), qMax(0.0, inX - lane.left()), lane.height()), shade);
    painter.fillRect(QRectF(qMax(lane.left(), outX), lane.top(), qMax(0.0, lane.right() - outX), lane.height()), shade);
    painter.restore();
}

void VideoTimeline::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();
    const qreal inX = xForTime(m_in);
    const qreal outX = xForTime(m_out);

    const QColor background = palette().color(QPalette::Window);
    const QColor foreground = palette().color(QPalette::WindowText);
    auto ink = [&](qreal amount) {
        return QColor::fromRgbF(background.redF() * (1 - amount) + foreground.redF() * amount,
                                background.greenF() * (1 - amount) + foreground.greenF() * amount,
                                background.blueF() * (1 - amount) + foreground.blueF() * amount);
    };
    painter.setPen(Qt::NoPen);
    QPainterPath clip;
    clip.addRoundedRect(track, 6, 6);
    painter.save();
    painter.setClipPath(clip);
    painter.fillRect(track, ink(0.12));
    const auto times = thumbnailTimes();
    for (int i = 0; i < times.size(); ++i) {
        const QRectF slot(track.left() + i * track.width() / times.size(), track.top(),
                          track.width() / times.size(), track.height());
        // Until its own thumbnail arrives, a slot shows the nearest one kept from
        // before an edit, zoom or resize, rather than a strip gone empty.
        const QImage image = thumbnailNear(times[i], nullptr);
        if (image.isNull()) continue;
        const qreal scale = qMax(slot.width() / image.width(), slot.height() / image.height());
        const QSizeF crop(slot.width() / scale, slot.height() / scale);
        painter.drawImage(slot, image, QRectF(QPointF(image.width() - crop.width(), image.height() - crop.height()) / 2, crop));
    }
    QColor shade = background;
    shade.setAlpha(205);
    painter.fillRect(QRectF(track.left(), track.top(), qMax(0.0, inX - track.left()), track.height()), shade);
    painter.fillRect(QRectF(qMax(track.left(), outX), track.top(),
                           qMax(0.0, track.right() - outX), track.height()), shade);
    if (!m_fragments.isEmpty()) {
        const QVector<Fragment> parts = fragments::expanded(m_fragments);
        const bool pickedKept = m_selectedFragment >= 0 && m_selectedFragment < parts.size()
            && !parts[m_selectedFragment].removed;
        QColor quiet = background;
        quiet.setAlpha(110);
        for (const TimePiece &piece : m_axis.pieces()) {
            const qreal x0 = xForTime(qRound64(piece.srcStart));
            const qreal x1 = track.left() + track.width() * (piece.outEnd() - m_viewStart) / (m_viewEnd - m_viewStart);
            // The other fragments step back while one is selected.
            if (pickedKept && fragments::indexAt(m_fragments, qRound64(piece.srcStart)) != m_selectedFragment)
                painter.fillRect(QRectF(x0, track.top(), x1 - x0, track.height()), quiet);
            // A 2 px seam between fragments.
            if (piece.outStart > 0) painter.fillRect(QRectF(x0 - 1, track.top(), 2, track.height()), background);
        }
    }
    painter.restore();
    if (waveformShown()) paintWaveform(painter, inX, outX, ink);

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
            // The level in mono, the motion a step quieter in the UI face; a gap
            // separates them, no glyph (studio plan 6.1).
            const QString scale = QString::number(z.scale, 'g', 3) + QStringLiteral("×");
            const QString motion = motionName(z.motion);
            const qreal scaleWidth = QFontMetricsF(mono).horizontalAdvance(scale);
            const qreal motionWidth = QFontMetricsF(label).horizontalAdvance(motion);
            const qreal left = qMax(x0, lane.left()) + 8;
            const qreal room = qMin(x1, lane.right()) - 8 - left;
            if (room >= scaleWidth) {
                painter.setPen(ink(selected ? 0.92 : 0.76));
                painter.setFont(mono);
                painter.drawText(QRectF(left, lane.top(), scaleWidth, lane.height()), Qt::AlignVCenter, scale);
            }
            if (room >= scaleWidth + 6 + motionWidth) {
                painter.setPen(ink(selected ? 0.7 : 0.55));
                painter.setFont(label);
                painter.drawText(QRectF(left + scaleWidth + 6, lane.top(), motionWidth, lane.height()),
                                 Qt::AlignVCenter, motion);
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
            painter.drawText(lane, Qt::AlignCenter, tr("Click or press Z to add a zoom"));
        }
        painter.restore();
        painter.setPen(Qt::NoPen);
    }

    if (!m_masks.isEmpty()) {
        const QRectF lane = maskLaneRect();
        QPainterPath laneShape;
        laneShape.addRoundedRect(lane, 6, 6);
        painter.save();
        painter.setClipPath(laneShape);
        painter.fillRect(lane, ink(0.07));
        QFont label = font();
        label.setPixelSize(theme::kFsMicro);
        painter.setFont(label);
        for (const MaskBlock &mask : std::as_const(m_masks)) {
            const qreal x0 = xForTime(mask.fromMs), x1 = xForTime(mask.toMs);
            if (x1 < lane.left() || x0 > lane.right()) continue;
            const QRectF block(x0, lane.top(), qMax<qreal>(2, x1 - x0), lane.height());
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink(mask.selected ? 0.22 : 0.15));
            painter.drawRoundedRect(block, 4, 4);
            const qreal left = qMax(x0, lane.left()) + 8;
            const qreal room = qMin(x1, lane.right()) - 8 - left;
            if (room >= QFontMetricsF(label).horizontalAdvance(mask.label)) {
                painter.setPen(ink(mask.selected ? 0.9 : 0.7));
                painter.drawText(QRectF(left, lane.top(), room, lane.height()), Qt::AlignVCenter, mask.label);
            }
            if (mask.selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink(0.7));
                for (qreal x : {x0 + 3, x1 - 5})
                    painter.drawRoundedRect(QRectF(x, lane.center().y() - 5, 2, 10), 1, 1);
            }
        }
        painter.restore();
        painter.setPen(Qt::NoPen);
    }

    const qreal playX = xForTime(m_position);
    painter.setBrush(ink(0.86));
    if (playX >= track.left() && playX <= track.right()) {
        const qreal playBottom = !m_masks.isEmpty() ? maskLaneRect().bottom()
            : m_laneVisible ? zoomLaneRect().bottom() : waveformShown() ? waveformRect().bottom() : track.bottom() + 3;
        painter.drawRoundedRect(QRectF(playX - 1, track.top(), 2, playBottom - track.top()), 1, 1);
        painter.drawRoundedRect(QRectF(playX - 3, track.top() - 4, 6, 5), 2, 2);
    }
    auto handle = [&](qreal x, Drag kind) {
        if (x < track.left() || x > track.right()) return;
        const bool active = m_drag == kind || m_hover == kind;
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink(active ? 0.36 : 0.23));
        painter.drawRoundedRect(QRectF(x - 5, track.top() - 1, 10, track.height() + 2), 4, 4);
        // Brackets point into the kept range and stay distinct from the playhead.
        const qreal direction = kind == Drag::In ? 1 : -1;
        const qreal y = track.center().y();
        QPainterPath bracket;
        bracket.moveTo(x + direction * 2, y - 5);
        bracket.lineTo(x - direction * 1, y - 5);
        bracket.lineTo(x - direction * 1, y + 5);
        bracket.lineTo(x + direction * 2, y + 5);
        painter.setPen(QPen(ink(active ? 0.95 : 0.8), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(bracket);
    };
    handle(inX, Drag::In);
    handle(outX, Drag::Out);

    QFont boundaryFont = font(); boundaryFont.setPixelSize(theme::kFsMicro);
    const QFontMetricsF boundaryMetrics(boundaryFont);
    const QString start = tr("Start"), end = tr("End");
    auto labelRect = [&](qreal x, const QString &text, bool before) {
        if (x < track.left() || x > track.right()) return QRectF();
        const qreal w = boundaryMetrics.horizontalAdvance(text);
        return QRectF(qBound(track.left(), before ? x - w - 4 : x + 4, track.right() - w), 0, w, 13);
    };
    QRectF startLabel = labelRect(inX, start, true);
    QRectF endLabel = labelRect(outX, end, false);
    if (!startLabel.isEmpty() && !endLabel.isEmpty() && startLabel.right() + 8 > endLabel.left()) {
        endLabel.moveLeft(qMin(track.right() - endLabel.width(), startLabel.right() + 8));
        startLabel.moveRight(qMin(startLabel.right(), endLabel.left() - 8));
    }
    painter.setPen(ink(0.6));
    QFont ruler = font(); ruler.setPixelSize(theme::kFsMicro); painter.setFont(ruler);
    const auto ticks = rulerTicks();
    const qint64 step = ticks.size() > 1 ? ticks[1].second - ticks[0].second : 1000;
    for (const auto &[x, t] : ticks) {
        const QString label = QStringLiteral("%1:%2").arg(t / 60000)
            .arg((t / 1000) % 60, 2, 10, QLatin1Char('0'))
            + (step < 1000 ? QStringLiteral(".%1").arg((t % 1000) / 100) : QString());
        const QRectF tickLabel(x - 30, 0, 60, 13);
        if ((!startLabel.isEmpty() && tickLabel.intersects(startLabel.adjusted(-4, 0, 4, 0)))
            || (!endLabel.isEmpty() && tickLabel.intersects(endLabel.adjusted(-4, 0, 4, 0)))) continue;
        painter.drawText(tickLabel, Qt::AlignCenter, label);
    }
    painter.setFont(boundaryFont);
    painter.setPen(ink(0.85));
    if (!startLabel.isEmpty()) painter.drawText(startLabel, Qt::AlignCenter, start);
    if (!endLabel.isEmpty()) painter.drawText(endLabel, Qt::AlignCenter, end);
    if (m_in < m_viewStart) painter.drawText(QRectF(0, 19, 14, 26), Qt::AlignCenter, QStringLiteral("‹"));
    if (m_out > m_viewEnd) painter.drawText(QRectF(width() - 14, 19, 14, 26), Qt::AlignCenter, QStringLiteral("›"));
    // A cut is a notch in the ruler over its seam (E3); hover names it.
    const QVector<Fragment> parts = fragments::expanded(m_fragments);
    for (int i = 0; i < parts.size(); ++i) {
        if (!parts[i].removed) continue;
        const qreal x = xForTime(parts[i].startMs);
        if (x < track.left() - 4 || x > track.right() + 4) continue;
        const bool lit = i == m_hoverCut || i == m_selectedFragment;
        QPainterPath notch;
        notch.moveTo(x - 4, track.top() - 6);
        notch.lineTo(x + 4, track.top() - 6);
        notch.lineTo(x, track.top() - 1);
        notch.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.fillPath(notch, ink(lit ? 0.95 : 0.6));
        if (i == m_hoverCut) {
            const double ms = double(fragments::endOf(m_fragments, i, m_duration) - parts[i].startMs);
            const QString text = tr("Cut %1 s, click to restore").arg(ms / 1000.0, 0, 'f', 1);
            QFont hint = font();
            hint.setPixelSize(theme::kFsMicro);
            painter.setFont(hint);
            const qreal w = QFontMetricsF(hint).horizontalAdvance(text) + 8;
            const QRectF box(qBound(0.0, x + 6, width() - w), 0, w, 13);
            painter.fillRect(box, background);
            painter.setPen(ink(0.9));
            painter.drawText(box, Qt::AlignCenter, text);
        }
    }
    if (hasFocus()) {
        painter.setPen(QPen(ink(0.4), 1)); painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(-2, -2, 2, 2), 7, 7);
    }
}

} // namespace eddy
