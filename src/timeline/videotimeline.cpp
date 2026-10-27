#include "timeline/videotimeline.h"
#include "timeline/audiowaveform.h"
#include "app/theme.h"
#include "app/spring.h"
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

VideoTimeline::VideoTimeline(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("VideoTimeline"));
    m_height = new Spring(motion::kSlow, [this](qreal height) { setFixedHeight(qRound(height)); }, this);
    m_height->jumpTo(kBaseH);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Video timeline"));
    setToolTip(QStringLiteral("Drag to seek or pull the ends to trim\nFine trim\tShift\nZoom\tCtrl+wheel"));
    m_edgePan.setInterval(33);
    connect(&m_edgePan, &QTimer::timeout, this, [this] {
        if (!interacting()) return;
        const qreal edge = m_pointerX < 12 ? -1 : (m_pointerX > width() - 12 ? 1 : 0);
        if (!edge) return;
        const qint64 before = m_viewStart;
        panBy(qRound64(edge * (m_viewEnd - m_viewStart) * 0.015));
        m_dragTime += m_viewStart - before;
        moveDrag(m_pointerX, m_modifiers);
    });
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](QPoint pos) {
        emit hoverLeft();
        Drag part = Drag::None;
        if (const quint32 id = zoomAt(pos, &part)) {
            setSelectedZoom(id);
            emit zoomSelected(id);
            emit zoomMenuRequested(id, mapToGlobal(pos));
            return;
        }
        QMenu menu(this);
        theme::roundMenu(&menu);
        if (m_waveform) {
            QAction *show = menu.addAction(tr("Show waveform"));
            show->setCheckable(true);
            show->setChecked(m_waveformVisible);
            connect(show, &QAction::toggled, this, &VideoTimeline::setWaveformVisible);
            menu.addSeparator();
        }
        if (inZoomLane(pos)) {
            const qint64 time = timeForX(pos.x());
            menu.addAction(tr("Add zoom here"), this, [this, time] { emit zoomAddRequested(time); });
            menu.addSeparator();
        }
        menu.addAction(tr("Zoom in\t+"), this, [this] { zoomAt(2, m_position); });
        menu.addAction(tr("Zoom out\t−"), this, [this] { zoomAt(0.5, m_position); });
        menu.addAction(tr("Fit clip\t0"), this, &VideoTimeline::fitClip);
        menu.exec(mapToGlobal(pos));
    });
}

double VideoTimeline::edited(qint64 sourceMs) const {
    return m_fragments.isEmpty() ? double(sourceMs) : m_axis.toOutputAfter(double(sourceMs));
}

qint64 VideoTimeline::source(double editedMs) const {
    return m_fragments.isEmpty() ? qRound64(editedMs) : qRound64(m_axis.toSource(editedMs));
}

qint64 VideoTimeline::editedDuration() const {
    return m_fragments.isEmpty() ? m_duration : qRound64(m_axis.outputDurationMs());
}

void VideoTimeline::rebuildAxis() {
    ++m_waveRevision;
    m_axis = TimeMap(m_duration, 0, m_duration, m_fragments);
}

void VideoTimeline::setFragments(const QVector<Fragment> &fragments) {
    if (fragments == m_fragments) return;
    const bool fullView = m_viewStart == 0 && m_viewEnd == editedDuration();
    // The view keeps showing the same stretch of the source.
    const qint64 sourceStart = source(m_viewStart), sourceEnd = source(m_viewEnd);
    m_fragments = fragments;
    rebuildAxis();
    if (m_selectedFragment >= fragments::expanded(m_fragments).size()) m_selectedFragment = -1;
    m_hoverCut = -1;
    if (fullView) fitClip();
    else setViewRange(qRound64(edited(sourceStart)), qRound64(edited(sourceEnd)));
    emit viewRangeChanged();
    update();
}

void VideoTimeline::setSelectedFragment(int index) {
    if (index == m_selectedFragment) return;
    m_selectedFragment = index;
    update();
}

void VideoTimeline::ensureVisible(qint64 sourceMs) {
    const double e = edited(sourceMs);
    if (e >= m_viewStart && e <= m_viewEnd) return;
    const qint64 span = m_viewEnd - m_viewStart;
    setViewRange(qRound64(e), qRound64(e) + span);
}

int VideoTimeline::cutAt(QPointF pos) const {
    const QRectF track = trackRect();
    if (pos.y() > track.top() + 4) return -1;
    const QVector<Fragment> parts = fragments::expanded(m_fragments);
    for (int i = 0; i < parts.size(); ++i)
        if (parts[i].removed && qAbs(pos.x() - xForTime(parts[i].startMs)) <= 6) return i;
    return -1;
}

void VideoTimeline::setViewRange(qint64 startMs, qint64 endMs) {
    const qint64 total = editedDuration();
    const qint64 span = qBound<qint64>(qMin(total, m_minimumRange * 10), endMs - startMs, total);
    const qint64 start = qBound<qint64>(0, startMs, total - span);
    if (m_viewStart == start && m_viewEnd == start + span) return;
    m_viewStart = start;
    m_viewEnd = start + span;
    emit viewRangeChanged();
    update();
}

void VideoTimeline::zoomAt(qreal factor, qint64 anchorMs) {
    if (m_duration <= 0 || !std::isfinite(factor) || factor <= 0) return;
    anchorMs = qBound(m_viewStart, qRound64(edited(anchorMs)), m_viewEnd);
    const qint64 oldSpan = m_viewEnd - m_viewStart;
    const qint64 total = editedDuration();
    const qint64 span = qBound(qMin(total, m_minimumRange * 10), qRound64(oldSpan / factor), total);
    const qreal fraction = oldSpan > 0 ? qreal(anchorMs - m_viewStart) / oldSpan : 0.5;
    const qint64 start = anchorMs - qRound64(fraction * span);
    setViewRange(start, start + span);
}

void VideoTimeline::panBy(qint64 deltaMs) {
    setViewRange(m_viewStart + deltaMs, m_viewEnd + deltaMs);
}

void VideoTimeline::fitClip() { setViewRange(0, editedDuration()); }

QVector<qint64> VideoTimeline::thumbnailTimes() const {
    QVector<qint64> times;
    if (m_duration <= 0) return times;
    const int count = qBound(6, width() / 64, 24);
    for (int i = 0; i < count; ++i)
        times.append(qMin(m_duration - 1, source(m_viewStart + (i + 0.5) * (m_viewEnd - m_viewStart) / count)));
    return times;
}

void VideoTimeline::setThumbnail(qint64 timeMs, const QImage &image) {
    const QVector<qint64> times = thumbnailTimes();
    if (image.isNull() || !times.contains(timeMs)) return;
    m_thumbnails.insert(timeMs, image);
    // Older ones stand in while a new strip loads; beyond a few strips' worth, only the current stay.
    if (m_thumbnails.size() > 72)
        for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();)
            it = times.contains(it.key()) ? std::next(it) : m_thumbnails.erase(it);
    update();
}

QImage VideoTimeline::thumbnailNear(qint64 time, qint64 *sampleTime) const {
    auto nearest = m_thumbnails.cend();
    for (auto it = m_thumbnails.cbegin(); it != m_thumbnails.cend(); ++it)
        if (nearest == m_thumbnails.cend() || qAbs(it.key() - time) < qAbs(nearest.key() - time)) nearest = it;
    if (nearest == m_thumbnails.cend()) return {};
    if (sampleTime) *sampleTime = nearest.key();
    return nearest.value();
}


void VideoTimeline::setDuration(qint64 durationMs) {
    const bool fullView = m_viewStart == 0 && m_viewEnd == editedDuration();
    m_duration = qMax<qint64>(0, durationMs);
    rebuildAxis();
    m_position = qBound<qint64>(0, m_position, m_duration);
    if (m_out == 0 || m_out > m_duration) m_out = m_duration;
    setTrimRange(m_in, m_out);
    if (fullView) fitClip();
    else setViewRange(m_viewStart, m_viewEnd);
}

void VideoTimeline::setMinimumRange(qint64 durationMs) {
    m_minimumRange = qMax<qint64>(1, durationMs);
    setTrimRange(m_in, m_out);
}

void VideoTimeline::setPosition(qint64 positionMs) {
    if (interacting()) return;
    const qint64 position = qBound<qint64>(0, positionMs, m_duration);
    if (position == m_position) return;
    // Only the playhead moves: repaint its old and new column, not the whole
    // timeline, which playback would otherwise redraw with every frame.
    for (const qint64 at : {m_position, position}) {
        const qreal x = xForTime(at);
        update(QRectF(x - 5, 0, 10, height()).toAlignedRect());
    }
    m_position = position;
}

void VideoTimeline::setTrimRange(qint64 inMs, qint64 outMs) {
    if (m_duration <= 0) {
        m_in = m_out = 0;
        update();
        return;
    }
    const qint64 gap = qMin(m_minimumRange, m_duration);
    m_in = qBound<qint64>(0, inMs, m_duration - gap);
    m_out = qBound<qint64>(m_in + gap, outMs, m_duration);
    update();
}

QRectF VideoTimeline::trackRect() const {
    return QRectF(kInset, kTrackTop, qMax<qreal>(1, width() - 2 * kInset), kTrackH);
}

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

// A lane opens and closes like a section: the strip grows on motion::kSlow while the
// lanes keep their places, so the new one is uncovered rather than slid in.
// Unseen, it just takes the height.
void VideoTimeline::updateHeight() {
    const int height = kBaseH + (waveformShown() ? kWaveStep : 0) + (m_laneVisible ? kLaneStep : 0)
                     + (m_masks.isEmpty() ? 0 : kMaskStep);
    if (isVisible()) m_height->setTarget(height);
    else m_height->jumpTo(height);
}

bool VideoTimeline::inZoomLane(QPointF pos) const {
    const QRectF lane = zoomLaneRect();
    return !lane.isEmpty() && pos.y() >= lane.top() - 2 && pos.y() <= lane.bottom() + 2;
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

qreal VideoTimeline::xForTime(qint64 timeMs) const {
    const QRectF track = trackRect();
    if (m_viewEnd <= m_viewStart) return track.left();
    return track.left() + track.width() * (edited(timeMs) - m_viewStart) / (m_viewEnd - m_viewStart);
}

qint64 VideoTimeline::timeForX(qreal x) const {
    const QRectF track = trackRect();
    if (m_duration <= 0 || track.width() <= 0) return 0;
    const qreal fraction = qBound<qreal>(0, (x - track.left()) / track.width(), 1);
    return source(m_viewStart + fraction * (m_viewEnd - m_viewStart));
}

// Ticks count edited time at even x, so a cut or a fast fragment never
// bunches them up; the labels are the output's own clock.
QVector<QPair<qreal, qint64>> VideoTimeline::rulerTicks() const {
    QVector<QPair<qreal, qint64>> ticks;
    const QRectF track = trackRect();
    if (m_viewEnd <= m_viewStart) return ticks;
    const qreal rawStep = qMax<qreal>(1, (m_viewEnd - m_viewStart) * 70.0 / track.width());
    const qreal base = std::pow(10.0, std::floor(std::log10(rawStep)));
    const qint64 step = qMax<qint64>(1, qRound64(base * (rawStep / base <= 2 ? 2 : rawStep / base <= 5 ? 5 : 10)));
    for (qint64 t = (m_viewStart / step + 1) * step; t < m_viewEnd; t += step)
        ticks.append({track.left() + track.width() * qreal(t - m_viewStart) / (m_viewEnd - m_viewStart), t});
    return ticks;
}

void VideoTimeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_duration <= 0) return;
    setFocus(Qt::MouseFocusReason);
    if (const int cut = cutAt(event->position()); cut >= 0) {
        emit cutClicked(cut);
        event->accept();
        return;
    }
    {
        Drag part = Drag::None;
        const int mask = maskAt(event->position(), &part);
        if (mask >= 0) {
            emit maskSelected(m_masks[mask].key);
            m_drag = part;
            m_dragMask = mask;
            m_maskBefore = m_masks[mask];
            m_zoomMoved = false;
            m_dragX = m_pointerX = event->position().x();
            m_grabOffset = timeForX(m_dragX) - (part == Drag::MaskEnd ? m_masks[mask].toMs : m_masks[mask].fromMs);
            m_edgePan.start();
            event->accept();
            return;
        }
        if (!maskLaneRect().isEmpty() && event->position().y() >= maskLaneRect().top() - 2) {
            event->accept();   // the empty mask lane does nothing
            return;
        }
    }
    if (inZoomLane(event->position())) {
        Drag part = Drag::None;
        const quint32 id = zoomAt(event->position(), &part);
        if (!id) {
            emit zoomAddRequested(timeForX(event->position().x()));
            event->accept();
            return;
        }
        setSelectedZoom(id);
        emit zoomSelected(id);
        const ZoomSegment &z = m_zooms[zoomlane::indexOf(m_zooms, id)];
        m_drag = part;
        m_dragZoom = id;
        m_zoomsBefore = m_zooms;
        m_zoomMoved = false;
        m_dragX = m_pointerX = event->position().x();   // the edge-pan timer reads it
        m_modifiers = event->modifiers();
        m_grabOffset = timeForX(m_dragX) - (part == Drag::ZoomEnd ? z.endMs : z.startMs);
        m_edgePan.start();
        event->accept();
        return;
    }
    const qreal x = event->position().x();
    const qreal inDistance = edited(m_in) < m_viewStart || edited(m_in) > m_viewEnd ? width() + 20 : qAbs(x - (xForTime(m_in) - kInset / 2));
    const qreal outDistance = edited(m_out) < m_viewStart || edited(m_out) > m_viewEnd ? width() + 20 : qAbs(x - (xForTime(m_out) + kInset / 2));
    if (qMin(inDistance, outDistance) <= kGripReach)
        m_drag = inDistance <= outDistance ? Drag::In : Drag::Out;
    else
        m_drag = Drag::Seek;
    m_beforeIn = m_in; m_beforeOut = m_out; m_beforePosition = m_position;
    m_dragX = x;
    m_dragTime = edited(m_drag == Drag::In ? m_in : m_out);
    m_fine = event->modifiers().testFlag(Qt::ShiftModifier);
    emit interactionStarted(trimming());
    m_edgePan.start();
    mouseMoveEvent(event);
    event->accept();
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *event) {
    if (m_drag == Drag::None) {
        const int cut = cutAt(event->position());
        if (cut != m_hoverCut) {
            m_hoverCut = cut;
            update();
        }
        if (cut >= 0) {
            setCursor(Qt::PointingHandCursor);
            emit hoverLeft();
            return;
        }
        Drag maskPart = Drag::None;
        if (maskAt(event->position(), &maskPart) >= 0
            || (!maskLaneRect().isEmpty() && event->position().y() >= maskLaneRect().top() - 2)) {
            setCursor(maskPart == Drag::MaskStart || maskPart == Drag::MaskEnd ? Qt::SizeHorCursor
                                                                             : Qt::PointingHandCursor);
            emit hoverLeft();
            return;
        }
        if (inZoomLane(event->position())) {
            Drag part = Drag::None;
            zoomAt(event->position(), &part);
            setCursor(part == Drag::ZoomStart || part == Drag::ZoomEnd ? Qt::SizeHorCursor
                                                                     : Qt::PointingHandCursor);
            if (m_hover != Drag::None) { m_hover = Drag::None; update(); }
            emit hoverLeft();
            return;
        }
        const qreal x = event->position().x();
        const qreal inDistance = edited(m_in) < m_viewStart || edited(m_in) > m_viewEnd ? width() + 20 : qAbs(x - (xForTime(m_in) - kInset / 2));
        const qreal outDistance = edited(m_out) < m_viewStart || edited(m_out) > m_viewEnd ? width() + 20 : qAbs(x - (xForTime(m_out) + kInset / 2));
        const Drag hover = m_duration > 0 && qMin(inDistance, outDistance) <= kGripReach
            ? (inDistance <= outDistance ? Drag::In : Drag::Out) : Drag::None;
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
        setCursor(hover == Drag::None ? Qt::PointingHandCursor : Qt::SizeHorCursor);
        emit hoverRequested(timeForX(x), event->pos());
        return;
    }
    moveDrag(event->position().x(), event->modifiers());
    event->accept();
}

void VideoTimeline::moveDrag(qreal x, Qt::KeyboardModifiers modifiers) {
    m_pointerX = x;
    m_modifiers = modifiers;
    if (zoomDragging()) { moveZoomDrag(x); return; }
    if (maskDragging()) { moveMaskDrag(x); return; }
    const bool fine = modifiers.testFlag(Qt::ShiftModifier);
    if (fine != m_fine) {
        m_dragTime = edited(m_drag == Drag::In ? m_in : m_out);
        m_dragX = x;
        m_fine = fine;
    }
    const qint64 time = m_drag == Drag::Seek ? timeForX(x)
        : source(m_dragTime + (x - m_dragX) * (m_viewEnd - m_viewStart)
                                / trackRect().width() * (m_fine ? 0.1 : 1.0));
    if (m_drag == Drag::In) {
        m_in = qBound<qint64>(0, time, m_out - qMin(m_minimumRange, m_duration));
        emit trimPreviewed(m_in, m_out);
        m_position = m_in;
    } else if (m_drag == Drag::Out) {
        m_out = qBound<qint64>(m_in + qMin(m_minimumRange, m_duration), time, m_duration);
        emit trimPreviewed(m_in, m_out);
        m_position = qMax(m_in, m_out - 1);
    } else {
        m_position = time;
    }
    emit seekRequested(m_position);
    if (trimming()) emit hoverRequested(m_position, QPoint(qRound(x), int(kTrackTop)));
    update();
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_drag == Drag::None) return;
    if (maskDragging()) {
        moveMaskDrag(event->position().x());
        m_drag = Drag::None;
        m_edgePan.stop();
        const MaskBlock &mask = m_masks[m_dragMask];
        if (mask.fromMs != m_maskBefore.fromMs || mask.toMs != m_maskBefore.toMs)
            emit maskWindowEdited(mask.key, mask.fromMs, mask.toMs, m_maskBefore.fromMs, m_maskBefore.toMs);
        update();
        event->accept();
        return;
    }
    if (zoomDragging()) {
        moveZoomDrag(event->position().x());
        m_drag = Drag::None;
        m_edgePan.stop();
        if (m_zooms != m_zoomsBefore) emit zoomsEdited(m_zoomsBefore, m_zooms);
        update();
        event->accept();
        return;
    }
    moveDrag(event->position().x(), event->modifiers());
    const bool trimmed = m_drag == Drag::In || m_drag == Drag::Out;
    m_drag = Drag::None;
    m_edgePan.stop();
    if (trimmed && (m_in != m_beforeIn || m_out != m_beforeOut)) emit trimCommitted(m_in, m_out);
    emit interactionFinished(false);
    emit hoverLeft();
    update();
    event->accept();
}

void VideoTimeline::leaveEvent(QEvent *event) {
    m_hover = Drag::None;
    m_hoverCut = -1;
    if (!interacting()) emit hoverLeft();
    setCursor(Qt::PointingHandCursor);
    update();
    QWidget::leaveEvent(event);
}

void VideoTimeline::cancelInteraction() {
    if (!interacting()) return;
    if (maskDragging()) {
        m_drag = Drag::None;
        m_edgePan.stop();
        m_masks[m_dragMask] = m_maskBefore;
        emit maskWindowPreviewed(m_maskBefore.key, m_maskBefore.fromMs, m_maskBefore.toMs);
        update();
        return;
    }
    if (zoomDragging()) {
        m_drag = Drag::None;
        m_edgePan.stop();
        m_zooms = m_zoomsBefore;
        emit zoomsPreviewed(m_zooms);
        update();
        return;
    }
    m_drag = Drag::None;
    m_edgePan.stop();
    m_in = m_beforeIn; m_out = m_beforeOut; m_position = m_beforePosition;
    emit trimPreviewed(m_in, m_out);
    emit seekRequested(m_position);
    emit interactionFinished(true);
    emit hoverLeft();
    update();
}

void VideoTimeline::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        if (!interacting()) { QWidget::keyPressEvent(event); return; }
        cancelInteraction(); break;
    case Qt::Key_Plus: case Qt::Key_Equal: zoomAt(2, m_position); break;
    case Qt::Key_Minus: zoomAt(0.5, m_position); break;
    case Qt::Key_0: fitClip(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void VideoTimeline::wheelEvent(QWheelEvent *event) {
    const QPoint delta = event->pixelDelta().isNull() ? event->angleDelta() : event->pixelDelta() * 2;
    if (event->modifiers().testFlag(Qt::ControlModifier))
        zoomAt(std::pow(1.2, delta.y() / 120.0), timeForX(event->position().x()));
    else if (event->modifiers().testFlag(Qt::ShiftModifier) || delta.x())
        panBy(qRound64(-(delta.x() ? delta.x() : delta.y()) * (m_viewEnd - m_viewStart) / 1200.0));
    event->accept();
}

void VideoTimeline::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    emit viewRangeChanged();
}

void VideoTimeline::focusOutEvent(QFocusEvent *event) {
    cancelInteraction();
    emit hoverLeft();
    QWidget::focusOutEvent(event);
}

}
