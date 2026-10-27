#include "timeline/videotimeline.h"
#include "timeline/audiowaveform.h"
#include "app/theme.h"
#include "timeline/zoomlane.h"
#include "app/spring.h"
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
#include <algorithm>
#include <cmath>

namespace eddy {

// A grey envelope, the same up and down, with a brighter RMS core on a fixed
// dB scale. Stretches not read yet get a dotted line, never a flat "silence".
// Drawn once into a cache: the playhead repaints the timeline every frame.
void VideoTimeline::paintWaveform(QPainter &painter, const std::function<QColor(qreal)> &ink) {
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
            label.setPixelSize(theme::kFsCaption);
            p.setFont(label);
            p.setPen(palette().color(QPalette::PlaceholderText));   // sub: 6:1 where 0.45 gave 3.4:1
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
    painter.restore();
}

void VideoTimeline::paintEvent(QPaintEvent *) {
    syncStates();   // every state change repaints: here its spring is aimed
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();

    const QColor background = palette().color(QPalette::Window);
    const QColor foreground = palette().color(QPalette::WindowText);
    auto ink = [&](qreal amount) {
        return QColor::fromRgbF(background.redF() * (1 - amount) + foreground.redF() * amount,
                                background.greenF() * (1 - amount) + foreground.greenF() * amount,
                                background.blueF() * (1 - amount) + foreground.blueF() * amount);
    };
    painter.setPen(Qt::NoPen);
    // Every kept piece a block (K): its own thumbnails, a 2 px gap to the next, grips at
    // both ends. The block being pulled is drawn last, over a neighbour it grows onto.
    const QVector<QRectF> blocks = pieceRects();
    const QVector<TimePiece> &pieces = m_axis.pieces();
    const auto times = thumbnailTimes();
    const qreal slotWidth = times.isEmpty() ? track.width() : track.width() / times.size();
    const int picked = selectedPiece();
    QFont speedFont = font();
    speedFont.setPixelSize(theme::kFsCaption);
    const QFontMetricsF speedMetrics(speedFont);
    QVector<int> order;
    for (int i = 0; i < blocks.size(); ++i) if (!(m_drag == Drag::Edge && i == m_edgePiece)) order.append(i);
    if (m_drag == Drag::Edge && m_edgePiece >= 0 && m_edgePiece < blocks.size()) order.append(m_edgePiece);
    for (const int i : std::as_const(order)) {
        const QRectF block = blocks[i];
        if (block.right() < track.left() || block.left() > track.right() || block.width() <= 0) continue;
        const QPainterPath shape = theme::squircleRect(block, qMin<qreal>(theme::kRChip, block.width() / 2));
        painter.save();
        painter.setClipRect(track);
        painter.setClipPath(shape, Qt::IntersectClip);
        painter.fillRect(block, ink(0.12 + 0.06 * m_states->value(kFocus)));   // focus: a brighter strip, not a ring
        // Its slots move with it: each slot whose middle falls in the piece, shifted by the
        // block's glide. Until its own thumbnail arrives a slot shows the nearest kept one.
        const qreal shift = block.left() - kGap / 2 - xForOutput(pieces[i].outStart);
        const bool pulled = m_drag == Drag::Edge && i == m_edgePiece;
        // Every slot that touches the piece, with a picture from inside it: no bare end.
        for (int k = 0; k < times.size(); ++k) {
            const QRectF slot(track.left() + k * slotWidth + (pulled && !m_edgeEnd ? 0 : shift), track.top(), slotWidth, track.height());
            if (slot.right() <= block.left() || slot.left() >= block.right()) continue;
            const qint64 inside = std::clamp<qint64>(times[k], qint64(pieces[i].srcStart), qint64(pieces[i].srcEnd) - 1);
            const QImage image = thumbnailNear(inside, nullptr);
            if (image.isNull()) continue;
            const qreal scale = qMax(slot.width() / image.width(), slot.height() / image.height());
            const QSizeF crop(slot.width() / scale, slot.height() / scale);
            painter.drawImage(slot, image, QRectF(QPointF(image.width() - crop.width(), image.height() - crop.height()) / 2, crop));
        }
        if (pulled) {   // the frame at the pulled edge, right inside it
            const QImage image = thumbnailNear(m_position, nullptr);
            if (!image.isNull()) {
                const QRectF slot(m_edgeEnd ? block.right() - slotWidth : block.left(), track.top(), slotWidth, track.height());
                const qreal scale = qMax(slot.width() / image.width(), slot.height() / image.height());
                const QSizeF crop(slot.width() / scale, slot.height() / scale);
                painter.drawImage(slot, image, QRectF(QPointF(image.width() - crop.width(), image.height() - crop.height()) / 2, crop));
            }
        }
        // Grips inside the block: the clip's two ends always, an inner edge while it is
        // hovered or its piece chosen; brighter while pulled or under the pointer.
        const qreal chosen = m_states->value(stateKey(kPicked, quint64(i)));
        for (const bool isEnd : {false, true}) {
            const qreal active = m_states->value(stateKey(kEdge, quint64(i * 2 + int(isEnd))));
            const bool outer = isEnd ? i + 1 == blocks.size() : i == 0;
            const qreal shown = outer ? 1.0 : qMax(active, chosen);
            if (shown < 0.001) continue;
            const QRectF grip(isEnd ? block.right() - kInset : block.left(), track.top(), kInset, track.height());
            painter.setOpacity(shown);
            painter.fillRect(grip, ink(0.34 + 0.16 * active));
            painter.setBrush(ink(0.78 + 0.17 * active));
            painter.drawRoundedRect(QRectF(grip.center().x() - 1, grip.center().y() - 6, 2, 12), 1, 1);
            painter.setOpacity(1);
        }
        // Its speed, when not 1x, top left on a quiet backing, while there is room.
        if (pieces[i].speed != 1.0) {
            const QString speed = QString::number(pieces[i].speed, 'g', 3) + QStringLiteral("\u00d7");
            const qreal w = speedMetrics.horizontalAdvance(speed) + 2 * theme::kS1;
            const QRectF label(block.left() + kInset + theme::kS1, track.top() + theme::kS1, w, theme::kFsCaption + theme::kS1);
            if (label.right() + theme::kS1 <= block.right() - kInset) {
                QColor backing = background;
                backing.setAlphaF(0.78);
                painter.setBrush(backing);
                painter.drawRoundedRect(label, 3, 3);
                painter.setFont(speedFont);
                painter.setPen(foreground);
                painter.drawText(label, Qt::AlignCenter, speed);
                painter.setPen(Qt::NoPen);
            }
        }
        painter.restore();
        // Chosen: a 2 px ring in the text colour, inside the block, easing in.
        if (chosen > 0.001) {
            painter.save();
            painter.setOpacity(chosen);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(foreground, 2));
            painter.setClipRect(track);
            painter.drawPath(theme::squircleRect(block.adjusted(1, 1, -1, -1), qMin<qreal>(theme::kRChip - 1, block.width() / 2)));
            painter.restore();
            painter.setPen(Qt::NoPen);
        }
    }
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(track, 6, 6);
    painter.setClipPath(clip);
    // The marked range (C1): one step lighter on the strip and the waveform, no line.
    const qreal markOpacity = m_markFade->value();
    QRectF markColumns;
    if (markOpacity > 0.001 && m_markShown.second > m_markShown.first) {
        const qreal x0 = qMax(track.left(), xForTime(m_markShown.first));
        const qreal x1 = qMin(track.right(), xForTime(m_markShown.second));
        markColumns = QRectF(x0, 0, qMax<qreal>(0, x1 - x0), height());
        QColor lift = foreground;
        lift.setAlphaF(kMarkLift * markOpacity);
        painter.fillRect(QRectF(x0, track.top(), markColumns.width(), track.height()), lift);
    }
    painter.restore();
    if (waveformShown()) {
        paintWaveform(painter, ink);
        // The blocks' gaps run on through the waveform.
        const QRectF lane = waveformRect();
        for (int i = 1; i < blocks.size(); ++i)
            painter.fillRect(QRectF(blocks[i].left() - kGap, lane.top(), kGap, lane.height()), background);
    }
    if (!markColumns.isEmpty() && waveformShown()) {
        QColor lift = foreground;
        lift.setAlphaF(kMarkLift * markOpacity);
        const QRectF lane = waveformRect();
        QPainterPath laneShape;
        laneShape.addRoundedRect(lane, 6, 6);
        painter.save();
        painter.setClipPath(laneShape);
        painter.fillRect(QRectF(markColumns.left(), lane.top(), markColumns.width(), lane.height()), lift);
        painter.restore();
    }

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
                             ink(0.13 + 0.07 * m_states->value(stateKey(kZoom, z.id))));
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
        mono.setPixelSize(theme::kFsCaption);
        QFont label = font();
        label.setPixelSize(theme::kFsCaption);
        for (const ZoomSegment &z : std::as_const(m_zooms)) {
            const qreal x0 = xForTime(z.startMs), x1 = xForTime(z.endMs);
            if (x1 < lane.left() || x0 > lane.right()) continue;
            const qreal chosen = m_states->value(stateKey(kZoom, z.id));
            // The level in mono, the motion a step quieter in the UI face; a gap
            // separates them, no glyph (studio plan 6.1).
            const QString scale = QString::number(z.scale, 'g', 3) + QStringLiteral("×");
            const QString motion = motionName(z.motion);
            const qreal scaleWidth = QFontMetricsF(mono).horizontalAdvance(scale);
            const qreal motionWidth = QFontMetricsF(label).horizontalAdvance(motion);
            const qreal left = qMax(x0, lane.left()) + 8;
            const qreal room = qMin(x1, lane.right()) - 8 - left;
            if (room >= scaleWidth) {
                painter.setPen(ink(0.76 + 0.16 * chosen));
                painter.setFont(mono);
                painter.drawText(QRectF(left, lane.top(), scaleWidth, lane.height()), Qt::AlignVCenter, scale);
            }
            if (room >= scaleWidth + 6 + motionWidth) {
                painter.setPen(ink(0.55 + 0.15 * chosen));
                painter.setFont(label);
                painter.drawText(QRectF(left + scaleWidth + 6, lane.top(), motionWidth, lane.height()),
                                 Qt::AlignVCenter, motion);
            }
            if (chosen > 0.001) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink(0.7));
                painter.setOpacity(chosen);
                for (qreal x : {x0 + 3, x1 - 5})
                    painter.drawRoundedRect(QRectF(x, lane.center().y() - 6, 2, 12), 1, 1);
                painter.setOpacity(1);
            }
        }
        if (m_zooms.isEmpty()) {
            painter.setFont(label);
            painter.setPen(palette().color(QPalette::PlaceholderText));   // sub: 6:1 where 0.45 gave 3.4:1
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
        label.setPixelSize(theme::kFsCaption);
        painter.setFont(label);
        for (const MaskBlock &mask : std::as_const(m_masks)) {
            const qreal x0 = xForTime(mask.fromMs), x1 = xForTime(mask.toMs);
            if (x1 < lane.left() || x0 > lane.right()) continue;
            const QRectF block(x0, lane.top(), qMax<qreal>(2, x1 - x0), lane.height());
            painter.setPen(Qt::NoPen);
            const qreal chosen = m_states->value(stateKey(kMask, mask.key));
            painter.setBrush(ink(0.15 + 0.07 * chosen));
            painter.drawRoundedRect(block, 4, 4);
            const qreal left = qMax(x0, lane.left()) + 8;
            const qreal room = qMin(x1, lane.right()) - 8 - left;
            if (room >= QFontMetricsF(label).horizontalAdvance(mask.label)) {
                painter.setPen(ink(0.7 + 0.2 * chosen));
                painter.drawText(QRectF(left, lane.top(), room, lane.height()), Qt::AlignVCenter, mask.label);
            }
            if (chosen > 0.001) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(ink(0.7));
                painter.setOpacity(chosen);
                for (qreal x : {x0 + 3, x1 - 5})
                    painter.drawRoundedRect(QRectF(x, lane.center().y() - 5, 2, 10), 1, 1);
                painter.setOpacity(1);
            }
        }
        painter.restore();
        painter.setPen(Qt::NoPen);
    }

    // A bound beyond the view: a chevron in the inset, pointing to where it is.
    auto chevron = [&](qreal x, qreal direction) {
        const qreal y = track.center().y();
        QPainterPath mark;
        mark.moveTo(x + direction * 2, y - 4);
        mark.lineTo(x - direction * 2, y);
        mark.lineTo(x + direction * 2, y + 4);
        painter.setPen(QPen(ink(0.78), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(mark);
    };
    // More clip beyond the view.
    if (m_viewStart > 0) chevron(kInset / 2, 1);
    if (m_viewEnd < editedDuration()) chevron(width() - kInset / 2, -1);

    // The playhead over everything, its head in the gap above the strip.
    const qreal playX = xForTime(m_position);
    if (playX >= track.left() && playX <= track.right()) {
        const qreal playBottom = !m_masks.isEmpty() ? maskLaneRect().bottom()
            : m_laneVisible ? zoomLaneRect().bottom() : waveformShown() ? waveformRect().bottom() : track.bottom();
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink(0.92));
        painter.drawRoundedRect(QRectF(playX - 1, track.top(), 2, playBottom - track.top()), 1, 1);
        painter.drawRoundedRect(QRectF(playX - 4, track.top() - 6, 8, 6), 2, 2);
    }

    // Hints in the ruler (a hovered cut, the mark's length) are placed first: a tick
    // label under one is left out rather than shown cut in half.
    QFont ruler = font(); ruler.setPixelSize(theme::kFsCaption);
    const QFontMetricsF rulerMetrics(ruler);
    const QVector<Fragment> parts = fragments::expanded(m_fragments);
    QString cutHint, markHint;
    QRectF cutBox, markBox;
    if (m_hoverCut >= 0 && m_hoverCut < parts.size() && parts[m_hoverCut].removed) {
        const qreal x = xForTime(parts[m_hoverCut].startMs);
        const double ms = double(fragments::endOf(m_fragments, m_hoverCut, m_duration) - parts[m_hoverCut].startMs);
        cutHint = tr("Cut %1\u00a0s, click to restore").arg(ms / 1000.0, 0, 'f', 1);
        const qreal w = rulerMetrics.horizontalAdvance(cutHint) + 8;
        cutBox = QRectF(qBound(0.0, x + 6, width() - w), 0, w, kRulerH);
    } else if (!markColumns.isEmpty()) {
        const double seconds = (edited(m_markShown.second) - edited(m_markShown.first)) / 1000.0;
        markHint = tr("%1\u00a0s, Delete cuts").arg(seconds, 0, 'f', 1);
        const qreal w = rulerMetrics.horizontalAdvance(markHint) + 8;
        markBox = QRectF(qBound(0.0, markColumns.center().x() - w / 2, width() - w), 0, w, kRulerH);
    }
    const QRectF hintBox = (cutBox.isEmpty() ? markBox : cutBox).adjusted(-theme::kS1, 0, theme::kS1, 0);

    // Time labels only: each needs its own width plus kS3 of air, and none is cut
    // at the widget's edge. The grips mark the kept range; the fields below name it.
    painter.setPen(ink(0.6));
    painter.setFont(ruler);
    const auto ticks = rulerTicks();
    const qint64 step = ticks.size() > 1 ? ticks[1].second - ticks[0].second : 1000;
    qreal lastRight = -1e9;
    for (const auto &[x, t] : ticks) {
        const QString label = QStringLiteral("%1:%2").arg(t / 60000)
            .arg((t / 1000) % 60, 2, 10, QLatin1Char('0'))
            + (step < 1000 ? QStringLiteral(".%1").arg((t % 1000) / 100) : QString());
        const qreal w = rulerMetrics.horizontalAdvance(label);
        const QRectF tickLabel(x - w / 2, 0, w, kRulerH);
        if (tickLabel.left() < 0 || tickLabel.right() > width() || tickLabel.left() < lastRight + theme::kS3) continue;
        if (!hintBox.isEmpty() && tickLabel.intersects(hintBox)) continue;
        painter.drawText(tickLabel, Qt::AlignCenter, label);
        lastRight = tickLabel.right();
    }
    // A cut is a notch in the ruler over its seam (E3); hover names it.
    for (int i = 0; i < parts.size(); ++i) {
        if (!notchShown(i)) continue;
        const qreal x = xForTime(parts[i].startMs);
        if (x < track.left() - 4 || x > track.right() + 4) continue;
        const qreal lit = m_states->value(stateKey(kNotch, quint64(i)));
        QPainterPath notch;
        notch.moveTo(x - 4, track.top() - 6);
        notch.lineTo(x + 4, track.top() - 6);
        notch.lineTo(x, track.top() - 1);
        notch.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.fillPath(notch, ink(0.6 + 0.35 * lit));
    }
    if (!cutHint.isEmpty()) {
        painter.fillRect(cutBox, background);
        painter.setPen(ink(0.9));
        painter.drawText(cutBox, Qt::AlignCenter, cutHint);
    }
    // The mark's length in the output, over it in the ruler: what Delete takes out.
    if (!markHint.isEmpty()) {
        painter.setOpacity(markOpacity);
        painter.fillRect(markBox, background);
        painter.setPen(ink(0.9));
        painter.drawText(markBox, Qt::AlignCenter, markHint);
        painter.setOpacity(1);
    }
}

} // namespace eddy
