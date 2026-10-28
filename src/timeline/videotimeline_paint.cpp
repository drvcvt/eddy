// Painting the timeline: the ghosts, pauses, blocks and their grips, the mark, the ruler
// with its hints, the playhead. The lanes paint themselves (videotimeline_lanes.cpp).
#include "timeline/videotimeline.h"
#include "app/theme.h"
#include "app/spring.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace eddy {

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
    // A length on the quiet backing the speed label uses, centred, when it fits.
    const auto lengthLabel = [&](const QRectF &area, const QString &label) {
        const qreal w = speedMetrics.horizontalAdvance(label) + 2 * theme::kS1;
        if (w + 2 * theme::kS1 > area.width()) return;
        const QRectF box(area.center().x() - w / 2, area.center().y() - (theme::kFsCaption + theme::kS1) / 2.0,
                         w, theme::kFsCaption + theme::kS1);
        QColor backing = background;
        backing.setAlphaF(0.78);
        painter.setBrush(backing);
        painter.drawRoundedRect(box, 3, 3);
        painter.setFont(speedFont);
        painter.setPen(foreground);
        painter.drawText(box, Qt::AlignCenter, label);
        painter.setPen(Qt::NoPen);
    };
    const bool darkTheme = background.lightness() < 128;
    // The strip's slots over `area`, unmoved: what a ghost shows, and a block that does not glide.
    const auto slotFrames = [&](const QRectF &area) {
        for (int k = 0; k < times.size(); ++k) {
            const QRectF slot(track.left() + k * slotWidth, track.top(), slotWidth, track.height());
            if (slot.right() <= area.left() || slot.left() >= area.right()) continue;
            const QImage image = thumbnailNear(times[k], nullptr);
            if (image.isNull()) continue;
            const qreal scale = qMax(slot.width() / image.width(), slot.height() / image.height());
            const QSizeF crop(slot.width() / scale, slot.height() / scale);
            painter.save();
            painter.setClipRect(area, Qt::IntersectClip);
            painter.drawImage(slot, image, QRectF(QPointF(image.width() - crop.width(), image.height() - crop.height()) / 2, crop));
            painter.restore();
        }
    };
    // G: the ghost's look: recessed like a pause, its own frames far back, struck through
    // finely (a pause holds one whole frame). Under the pointer it comes a step forward.
    const auto ghostLook = [&](const QRectF &area, qreal lit) {
        const qreal opacity = painter.opacity();
        painter.fillRect(area, theme::tone(theme::Tone::Well, darkTheme));
        painter.setOpacity(opacity * (0.22 + 0.12 * lit));
        slotFrames(area);
        painter.setOpacity(opacity);
        QColor hatch = foreground;
        hatch.setAlphaF(0.14 + 0.06 * lit);
        painter.save();
        painter.setClipRect(area, Qt::IntersectClip);
        painter.setPen(QPen(hatch, 1));
        for (qreal x = area.left() - area.height(); x < area.right(); x += 6)
            painter.drawLine(QPointF(x, area.bottom()), QPointF(x + area.height(), area.top()));
        painter.restore();
    };
    // Where an edit just cut or took back source, the old look over the new, fading out.
    const qreal fresh = m_fresh->value();
    const auto freshParts = [&](const QVector<std::pair<qint64, qint64>> &spans, const QRectF &area) {
        QVector<QRectF> parts;
        if (fresh < 0.001) return parts;
        for (const auto &[a, b] : spans)
            if (const QRectF part = QRectF(QPointF(xForTime(a), area.top()), QPointF(xForTime(b), area.bottom())).intersected(area);
                !part.isEmpty())
                parts.append(part);
        return parts;
    };
    // G: what is cut stays where it was, and how much it is ("-1.2 s").
    const qreal msPerPx = qreal(m_viewEnd - m_viewStart) / qMax<qreal>(1, track.width());
    for (const QRectF &ghost : ghostRects()) {
        if (ghost.right() < track.left() || ghost.left() > track.right()) continue;
        qint64 from = 0;   // its source start, the key its state eases under
        for (const TimePiece &p : pieces) if (p.srcEnd <= timeForX(ghost.center().x())) from = qint64(p.srcEnd);
        const QPainterPath shape = theme::squircleRect(ghost, qMin<qreal>(theme::kRChip, ghost.width() / 2));
        painter.save();
        painter.setClipRect(track);
        painter.setClipPath(shape, Qt::IntersectClip);
        ghostLook(ghost, m_states->value(stateKey(kGhost, quint64(from))));
        const QVector<QRectF> wasKept = freshParts(m_freshCut, ghost);
        painter.setOpacity(fresh);
        for (const QRectF &part : wasKept) {
            painter.fillRect(part, ink(0.12));
            slotFrames(part);
        }
        // Its span on the strip is its source length: a ghost plays at 1x.
        painter.setOpacity(wasKept.isEmpty() ? 1 : 1 - fresh);
        lengthLabel(ghost, QStringLiteral("\u2212%1\u00a0s").arg((ghost.width() + kGap) * msPerPx / 1000.0, 0, 'f', 1));
        painter.restore();
    }
    // P: a pause is no block but a recessed stretch: the frame it holds, dimmed (or black),
    // and its length when it fits.
    for (int i = 0; i < pieces.size(); ++i) {
        const QRectF pause = pauseRect(i);
        if (pause.isEmpty() || pause.right() < track.left() || pause.left() > track.right()) continue;
        const bool moved = m_drag == Drag::Move && i == m_movePiece;
        const double length = moved ? m_moveNow : pieces[i].pauseBefore;
        const QPainterPath shape = theme::squircleRect(pause, qMin<qreal>(theme::kRChip, pause.width() / 2));
        painter.save();
        painter.setClipRect(track);
        painter.setClipPath(shape, Qt::IntersectClip);
        painter.fillRect(pause, pieces[i].pauseBlack ? QColor(Qt::black) : theme::tone(theme::Tone::Well, darkTheme));
        if (!pieces[i].pauseBlack) {
            const qint64 held = i > 0 ? qint64(pieces[i - 1].srcEnd) - 1 : qint64(pieces[i].srcStart);
            const QImage image = thumbnailNear(held, nullptr);
            if (!image.isNull()) {
                const qreal scale = qMax(pause.width() / image.width(), pause.height() / image.height());
                const QSizeF crop(pause.width() / scale, pause.height() / scale);
                painter.setOpacity(0.3);   // held, not playing: a step back from the blocks
                painter.drawImage(pause, image, QRectF(QPointF(image.width() - crop.width(), image.height() - crop.height()) / 2, crop));
                painter.setOpacity(1);
            }
        }
        lengthLabel(pause, QStringLiteral("%1\u00a0s").arg(length / 1000.0, 0, 'f', 1));   // readable over any held frame
        painter.restore();
    }
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
        const qreal shift = block.left() - kGap / 2 - xForShown(shownPiece(i).outStart);
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
        painter.setOpacity(fresh);   // just taken back: the ghost it was, fading
        for (const QRectF &part : freshParts(m_freshKept, block)) ghostLook(part, 0);
        painter.setOpacity(1);
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
        // The strip's gaps run on through the waveform, and under a ghost its sound steps back
        // as far as its frames do (G).
        const QRectF lane = waveformRect();
        QColor back = background;
        back.setAlphaF(0.7);
        const QVector<QRectF> ghosts = ghostRects();
        for (const QRectF &ghost : ghosts)
            painter.fillRect(QRectF(ghost.left(), lane.top(), ghost.width(), lane.height()), back);
        QVector<QRectF> stretches = blocks + ghosts;
        for (int i = 0; i < pieces.size(); ++i) stretches.append(pauseRect(i));
        for (const QRectF &r : std::as_const(stretches)) {
            if (r.isEmpty()) continue;
            for (const qreal x : {r.left() - kGap, r.right()})
                if (x > lane.left() + kGap / 2 && x + kGap < lane.right() - kGap / 2)
                    painter.fillRect(QRectF(x, lane.top(), kGap, lane.height()), background);
        }
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

    if (m_laneVisible) paintZoomLane(painter, ink);

    if (!m_masks.isEmpty()) paintMaskLane(painter, ink);

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
    if (m_viewEnd < shownDuration()) chevron(width() - kInset / 2, -1);

    // The playhead over everything, its head in the gap above the strip.
    const qreal playX = m_pauseHead >= 0 ? xForShown(shownForOutput(m_pauseHead)) : xForTime(m_position);   // P: in a pause
    if (playX >= track.left() && playX <= track.right()) {
        const qreal playBottom = !m_masks.isEmpty() ? maskLaneRect().bottom()
            : m_laneVisible ? zoomLaneRect().bottom() : waveformShown() ? waveformRect().bottom() : track.bottom();
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink(0.92));
        painter.drawRoundedRect(QRectF(playX - 1, track.top(), 2, playBottom - track.top()), 1, 1);
        painter.drawRoundedRect(QRectF(playX - 4, track.top() - 6, 8, 6), 2, 2);
    }

    // The mark's length in the ruler is placed first: a tick label under it is left out
    // rather than shown cut in half.
    QFont ruler = font(); ruler.setPixelSize(theme::kFsCaption);
    const QFontMetricsF rulerMetrics(ruler);
    QString markHint;
    QRectF markBox;
    if (!markColumns.isEmpty()) {   // what the output loses: ghosts inside it count nothing
        const double seconds = (m_axis.toOutputAfter(m_markShown.second) - m_axis.toOutputAfter(m_markShown.first)) / 1000.0;
        markHint = tr("%1\u00a0s, Delete cuts").arg(seconds, 0, 'f', 1);
        const qreal w = rulerMetrics.horizontalAdvance(markHint) + 8;
        markBox = QRectF(qBound(0.0, markColumns.center().x() - w / 2, width() - w), 0, w, kRulerH);
    }
    const QRectF hintBox = markBox.adjusted(-theme::kS1, 0, theme::kS1, 0);

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
    // The mark's length in the output, over it in the ruler: what Delete takes out.
    if (!markHint.isEmpty()) {
        painter.setOpacity(markOpacity);
        painter.fillRect(markBox, background);
        painter.setPen(ink(0.9));
        painter.drawText(markBox, Qt::AlignCenter, markHint);
        painter.setOpacity(1);
    }
}

}
