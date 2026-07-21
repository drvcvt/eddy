#include "videotimeline.h"
#include <QMouseEvent>
#include <QPainter>

namespace eddy {

VideoTimeline::VideoTimeline(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("VideoTimeline"));
    setMinimumHeight(42);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Drag the ends to trim · click to seek"));
}

void VideoTimeline::setContactSheet(const QImage &image, int frameCount) {
    m_contactSheet = image;
    m_contactSheetFrames = image.isNull() ? 0 : qMax(1, frameCount);
    update();
}

void VideoTimeline::setDuration(qint64 durationMs) {
    m_duration = qMax<qint64>(0, durationMs);
    m_position = qBound<qint64>(0, m_position, m_duration);
    if (m_out == 0 || m_out > m_duration) m_out = m_duration;
    setTrimRange(m_in, m_out);
}

void VideoTimeline::setMinimumRange(qint64 durationMs) {
    m_minimumRange = qMax<qint64>(1, durationMs);
    setTrimRange(m_in, m_out);
}

void VideoTimeline::setPosition(qint64 positionMs) {
    const qint64 position = qBound<qint64>(0, positionMs, m_duration);
    if (position == m_position) return;
    m_position = position;
    update();
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
    const qreal trackHeight = m_contactSheet.isNull() ? 8 : qMax(8, height() - 8);
    return QRectF(9, (height() - trackHeight) / 2.0, qMax(1, width() - 18), trackHeight);
}

qreal VideoTimeline::xForTime(qint64 timeMs) const {
    const QRectF track = trackRect();
    if (m_duration <= 0) return track.left();
    return track.left() + track.width() * qBound<qreal>(0, qreal(timeMs) / m_duration, 1);
}

qint64 VideoTimeline::timeForX(qreal x) const {
    const QRectF track = trackRect();
    if (m_duration <= 0 || track.width() <= 0) return 0;
    const qreal fraction = qBound<qreal>(0, (x - track.left()) / track.width(), 1);
    return qRound64(fraction * m_duration);
}

void VideoTimeline::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();
    const qreal inX = xForTime(m_in);
    const qreal outX = xForTime(m_out);

    painter.setPen(Qt::NoPen);
    if (!m_contactSheet.isNull()) {
        painter.drawImage(track, m_contactSheet);
    }
    QColor outside = palette().color(QPalette::PlaceholderText);
    outside.setAlpha(m_contactSheet.isNull() ? 80 : 150);
    painter.setBrush(outside);
    painter.drawRoundedRect(track, 4, 4);
    QColor selected = palette().color(QPalette::WindowText);
    selected.setAlpha(m_contactSheet.isNull() ? 105 : 35);
    painter.setBrush(selected);
    painter.drawRoundedRect(QRectF(inX, track.top(), qMax<qreal>(1, outX - inX), track.height()), 4, 4);

    const QColor foreground = palette().color(QPalette::WindowText);
    painter.setBrush(foreground);
    painter.drawRoundedRect(QRectF(inX - 4, 3, 8, height() - 6), 4, 4);
    painter.drawRoundedRect(QRectF(outX - 4, 3, 8, height() - 6), 4, 4);

    const qreal playX = xForTime(m_position);
    painter.setBrush(foreground);
    painter.drawRoundedRect(QRectF(playX - 1, 1, 2, height() - 2), 1, 1);
}

void VideoTimeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_duration <= 0) return;
    const qreal x = event->position().x();
    const qreal inDistance = qAbs(x - xForTime(m_in));
    const qreal outDistance = qAbs(x - xForTime(m_out));
    if (qMin(inDistance, outDistance) <= 11)
        m_drag = inDistance <= outDistance ? Drag::In : Drag::Out;
    else
        m_drag = Drag::Seek;
    mouseMoveEvent(event);
    event->accept();
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *event) {
    if (m_drag == Drag::None) return;
    const qint64 time = timeForX(event->position().x());
    if (m_drag == Drag::In) {
        m_in = qBound<qint64>(0, time, m_out - qMin(m_minimumRange, m_duration));
        emit trimPreviewed(m_in, m_out);
        emit seekRequested(m_in);
    } else if (m_drag == Drag::Out) {
        m_out = qBound<qint64>(m_in + qMin(m_minimumRange, m_duration), time, m_duration);
        emit trimPreviewed(m_in, m_out);
        emit seekRequested(m_out);
    } else {
        m_position = time;
        emit seekRequested(time);
    }
    update();
    event->accept();
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_drag == Drag::None) return;
    const bool trimmed = m_drag == Drag::In || m_drag == Drag::Out;
    m_drag = Drag::None;
    if (trimmed) emit trimCommitted(m_in, m_out);
    event->accept();
}

}
