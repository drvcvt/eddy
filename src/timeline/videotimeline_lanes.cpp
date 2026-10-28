// The lanes under the film strip: the waveform, the zooms and the timed masks. Their
// geometry, what is under the pointer, dragging a block, and painting them.
#include "timeline/videotimeline.h"
#include "timeline/lanes/audiowaveform.h"
#include "app/theme.h"
#include "app/spring.h"
#include "timeline/lanes/zoomlane.h"
#include "chrome/motionicon.h"
#include <QFontDatabase>
#include <QHashFunctions>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace eddy {

void VideoTimeline::setWaveform(AudioWaveformProvider *waveform) {
    if (m_waveform == waveform) return;
    if (m_waveform) m_waveform->disconnect(this);
    m_waveform = waveform;
    // The provider is the timeline's child and goes with it; the QPointer is all it needs.
    if (waveform) connect(waveform, &AudioWaveformProvider::changed, this, [this] {
        ++m_waveRevision;
        if (m_waveform->state() != AudioWaveformProvider::State::Loading) updateHeight();
        update();
    });
    updateHeight();
    update();
}

void VideoTimeline::setWaveformVisible(bool visible) {
    if (visible == m_waveformVisible) return;
    cancelInteraction();
    m_waveformVisible = visible;
    updateHeight();
    update();
}

AudioWaveformProvider *VideoTimeline::waveform() const { return m_waveform; }

bool VideoTimeline::waveformShown() const {
    return m_waveform && m_waveformVisible && !m_waveform->silent();
}

QRectF VideoTimeline::waveformRect() const {
    if (!waveformShown()) return {};
    const QRectF track = trackRect();
    return QRectF(track.left(), track.bottom() + theme::kS1, track.width(), kWaveH);
}

qreal VideoTimeline::belowWaveform() const {
    return waveformShown() ? waveformRect().bottom() : trackRect().bottom();
}

void VideoTimeline::setZoomLaneVisible(bool visible) {
    if (visible == m_laneVisible) return;
    cancelInteraction();
    m_laneVisible = visible;
    updateHeight();
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

bool VideoTimeline::inZoomLane(QPointF pos) const {
    const QRectF lane = zoomLaneRect();
    return !lane.isEmpty() && pos.y() >= lane.top() - 2 && pos.y() <= lane.bottom() + 2;
}

QRectF VideoTimeline::zoomLaneRect() const {
    if (!m_laneVisible) return {};
    const QRectF track = trackRect();
    return QRectF(track.left(), belowWaveform() + theme::kS1, track.width(), kLaneH);
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

void VideoTimeline::setMasks(const QVector<MaskBlock> &masks) {
    if (maskDragging() || masks == m_masks) return;   // the drag owns the list until it ends
    const bool shown = !m_masks.isEmpty();
    m_masks = masks;
    if (shown != !m_masks.isEmpty()) updateHeight();
    update();
}

QRectF VideoTimeline::maskLaneRect() const {
    if (m_masks.isEmpty()) return {};
    const QRectF track = trackRect();
    const qreal top = (m_laneVisible ? zoomLaneRect().bottom() : belowWaveform()) + theme::kS1;
    return QRectF(track.left(), top, track.width(), kMaskH);
}

int VideoTimeline::maskAt(QPointF pos, Drag *part) const {
    *part = Drag::None;
    const QRectF lane = maskLaneRect();
    if (lane.isEmpty() || pos.y() < lane.top() - 2 || pos.y() > lane.bottom() + 2) return -1;
    // The last drawn is on top.
    for (int i = m_masks.size() - 1; i >= 0; --i) {
        const qreal x0 = xForTime(m_masks[i].fromMs), x1 = xForTime(m_masks[i].toMs);
        const qreal d0 = qAbs(pos.x() - x0), d1 = qAbs(pos.x() - x1);
        if (qMin(d0, d1) <= 6) *part = d0 <= d1 ? Drag::MaskStart : Drag::MaskEnd;
        else if (pos.x() > x0 && pos.x() < x1) *part = Drag::MaskMove;
        else continue;
        return i;
    }
    return -1;
}

void VideoTimeline::moveMaskDrag(qreal x) {
    if (!m_zoomMoved && qAbs(x - m_dragX) < 3) return;
    m_zoomMoved = true;
    if (m_dragMask < 0 || m_dragMask >= m_masks.size()) return;
    MaskBlock &mask = m_masks[m_dragMask];
    const qint64 reach = qRound64(6.0 * (m_viewEnd - m_viewStart) / trackRect().width());
    QVector<qint64> anchors{m_position, m_in, m_out, 0, m_duration};
    for (int i = 0; i < m_masks.size(); ++i)
        if (i != m_dragMask) anchors << m_masks[i].fromMs << m_masks[i].toMs;
    const qint64 time = timeForX(x) - m_grabOffset;
    constexpr qint64 kMin = 100;
    if (m_drag == Drag::MaskMove) {
        const qint64 length = m_maskBefore.toMs - m_maskBefore.fromMs;
        qint64 start = zoomlane::snap(time, anchors, reach);
        const qint64 byEnd = zoomlane::snap(time + length, anchors, reach) - length;
        if (byEnd != time && (start == time || qAbs(byEnd - time) < qAbs(start - time))) start = byEnd;
        mask.fromMs = qBound<qint64>(0, start, m_duration - length);
        mask.toMs = mask.fromMs + length;
    } else if (m_drag == Drag::MaskStart) {
        mask.fromMs = qBound<qint64>(0, zoomlane::snap(time, anchors, reach), mask.toMs - kMin);
    } else {
        mask.toMs = qBound<qint64>(mask.fromMs + kMin, zoomlane::snap(time, anchors, reach), m_duration);
    }
    emit maskWindowPreviewed(mask.key, mask.fromMs, mask.toMs);
    update();
}

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

// The zoom lane: a block per zoom, the camera's level as a curve, labels where they fit.
void VideoTimeline::paintZoomLane(QPainter &painter, const std::function<QColor(qreal)> &ink) {
    const QColor foreground = palette().color(QPalette::WindowText);
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

// The mask lane: a block per timed redaction or spotlight, its label where it fits.
void VideoTimeline::paintMaskLane(QPainter &painter, const std::function<QColor(qreal)> &ink) {
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

}
