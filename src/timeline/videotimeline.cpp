// The timeline widget: its axis (the output, K), the view on it, the ruler, the thumbnails
// and the states it eases. Input, pieces, lanes and painting live beside it.
#include "timeline/videotimeline.h"
#include "app/theme.h"
#include "app/spring.h"
#include <cmath>

namespace eddy {

VideoTimeline::VideoTimeline(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("VideoTimeline"));
    m_height = new Spring(motion::kSlow, [this](qreal height) { setFixedHeight(qRound(height)); }, this);
    m_markFade = new Spring(motion::kFast, [this](qreal) { update(); }, this);   // a cleared mark goes on `fast`
    m_markFade->jumpTo(0);
    m_states = new SpringLevels(motion::kFast, [this] { update(); }, this);
    m_rippleGlide = new Spring(motion::kBase, [this](qreal) { update(); }, this);
    m_rippleGlide->jumpTo(0);
    m_glide = new Spring(motion::kBase, [this](qreal t) {
        m_gliding = true;
        setViewRange(qRound64(m_glideFrom.first + (m_glideTo.first - m_glideFrom.first) * t),
                     qRound64(m_glideFrom.second + (m_glideTo.second - m_glideFrom.second) * t));
        m_gliding = false;
    }, this);
    m_height->jumpTo(kBaseH);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Video timeline"));
    // The tooltip follows the area under the pointer (hintAt); over the strip the frame
    // preview is the hint and there is none.
    m_edgePan.setInterval(33);
    connect(&m_edgePan, &QTimer::timeout, this, [this] {
        if (!interacting()) return;
        const qreal edge = m_pointerX < theme::kS3 ? -1 : (m_pointerX > width() - theme::kS3 ? 1 : 0);
        if (!edge) return;
        const qint64 before = m_viewStart;
        panBy(qRound64(edge * (m_viewEnd - m_viewStart) * 0.015));
        m_dragTime += m_viewStart - before;
        moveDrag(m_pointerX, m_modifiers);
    });
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &VideoTimeline::contextMenu);
}

// The axis is the output (K): what is trimmed or cut is not on it.
double VideoTimeline::edited(qint64 sourceMs) const {
    return m_axis.pieces().isEmpty() ? double(sourceMs) : m_axis.toOutputAfter(double(sourceMs));
}

qint64 VideoTimeline::source(double editedMs) const {
    return m_axis.pieces().isEmpty() ? qRound64(editedMs) : qRound64(m_axis.toSource(editedMs));
}

qint64 VideoTimeline::editedDuration() const {
    return m_axis.pieces().isEmpty() ? m_duration : qRound64(m_axis.outputDurationMs());
}

void VideoTimeline::rebuildAxis() {
    ++m_waveRevision;
    m_axis = TimeMap(m_duration, m_in, m_out, m_fragments);
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

void VideoTimeline::setViewRange(qint64 startMs, qint64 endMs) {
    if (!m_gliding && m_glide && m_glide->state() == QAbstractAnimation::Running) m_glide->stop();   // the wheel takes over
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
    const int count = qBound(6, width() / (2 * int(kTrackH)), 24);   // slots about 2:1 on the strip
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
    m_markAnchor = m_markHead = -1;
    m_markOpen = false;
    m_markFade->jumpTo(0);
    m_position = qBound<qint64>(0, m_position, m_duration);
    if (m_out == 0 || m_out > m_duration) m_out = m_duration;
    setTrimRange(m_in, m_out);
    rebuildAxis();   // setTrimRange skips an unchanged range, and the duration under it changed
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
    if (m_markOpen) {   // an open mark grows with the playhead
        m_markHead = position;
        m_position = position;
        showMark();
        emit markChanged();
        update();
        return;
    }
    // Only the playhead moves: repaint its old and new column, not the whole
    // timeline, which playback would otherwise redraw with every frame.
    for (const qint64 at : {m_position, position}) {
        const qreal x = xForTime(at);
        update(QRectF(x - 5, 0, 10, height()).toAlignedRect());
    }
    if (m_pauseHead >= 0) {   // P: out of a pause again
        m_pauseHead = -1;
        update();
    }
    m_position = position;
}

QRectF VideoTimeline::trackRect() const {
    return QRectF(kInset, kTrackTop, qMax<qreal>(1, width() - 2 * kInset), kTrackH);
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

// Runs the change at once to learn where it ends, then glides there from here.
void VideoTimeline::glideView(const std::function<void()> &change) {
    const std::pair<qint64, qint64> from{m_viewStart, m_viewEnd};
    change();
    const std::pair<qint64, qint64> to{m_viewStart, m_viewEnd};
    if (!isVisible() || !Spring::enabled() || from == to) return;
    m_glideFrom = from;
    m_glideTo = to;
    m_gliding = true;
    setViewRange(from.first, from.second);
    m_gliding = false;
    m_glide->jumpTo(0);
    m_glide->setTarget(1);
}

void VideoTimeline::syncStates() {
    m_states->aim(kFocus, hasFocus());
    for (const ZoomSegment &z : std::as_const(m_zooms)) m_states->aim(stateKey(kZoom, z.id), z.id == m_selectedZoom);
    for (const MaskBlock &mask : std::as_const(m_masks)) m_states->aim(stateKey(kMask, mask.key), mask.selected);
    const QVector<Fragment> parts = fragments::expanded(m_fragments);
    for (int i = 0; i < parts.size(); ++i)
        m_states->aim(stateKey(kNotch, quint64(i)), i == m_hoverCut);
    const int picked = selectedPiece();
    for (int i = 0; i < m_axis.pieces().size(); ++i) {
        m_states->aim(stateKey(kPicked, quint64(i)), i == picked);
        for (const int edge : {i * 2, i * 2 + 1})
            m_states->aim(stateKey(kEdge, quint64(edge)), edge == m_hoverEdge
                          || (m_drag == Drag::Edge && edge == m_edgePiece * 2 + int(m_edgeEnd)));
    }
}

void VideoTimeline::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    emit viewRangeChanged();
}

void VideoTimeline::focusInEvent(QFocusEvent *event) {
    update();   // the track brightens on kFast
    QWidget::focusInEvent(event);
}

}
