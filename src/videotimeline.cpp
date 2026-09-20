#include "videotimeline.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace eddy {

VideoTimeline::VideoTimeline(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("VideoTimeline"));
    setFixedHeight(38);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("Video timeline"));
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
    return QRectF(6, 5, qMax(1, width() - 12), height() - 10);
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
    if (!m_contactSheet.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        // Crop each frame to its slot rather than stretching the contact sheet.
        const qreal sourceWidth = qreal(m_contactSheet.width()) / m_contactSheetFrames;
        const qreal slotWidth = track.width() / m_contactSheetFrames;
        const qreal scale = qMax(slotWidth / sourceWidth, track.height() / m_contactSheet.height());
        const QSizeF crop(slotWidth / scale, track.height() / scale);
        for (int i = 0; i < m_contactSheetFrames; ++i) {
            const QRectF source(i * sourceWidth + (sourceWidth - crop.width()) / 2,
                                (m_contactSheet.height() - crop.height()) / 2, crop.width(), crop.height());
            painter.drawImage(QRectF(track.left() + i * slotWidth, track.top(), slotWidth, track.height()),
                              m_contactSheet, source);
        }
    }
    QColor shade = background;
    shade.setAlpha(205);
    painter.fillRect(QRectF(track.left(), track.top(), inX - track.left(), track.height()), shade);
    painter.fillRect(QRectF(outX, track.top(), track.right() - outX, track.height()), shade);
    painter.restore();

    auto handle = [&](qreal x, Drag kind) {
        const bool active = m_drag == kind || m_hover == kind;
        painter.setBrush(ink(active ? 0.36 : 0.23));
        painter.drawRoundedRect(QRectF(x - 5, track.top() - 1, 10, track.height() + 2), 4, 4);
        painter.setBrush(ink(active ? 0.92 : 0.65));
        painter.drawRoundedRect(QRectF(x - 1, track.center().y() - 5, 2, 10), 1, 1);
    };
    handle(inX, Drag::In);
    handle(outX, Drag::Out);

    const qreal playX = xForTime(m_position);
    painter.setBrush(ink(0.86));
    painter.drawRoundedRect(QRectF(playX - 1, track.top(), 2, track.height() + 3), 1, 1);
    painter.drawRoundedRect(QRectF(playX - 3, 1, 6, 5), 2, 2);
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
    if (m_drag == Drag::None) {
        const qreal x = event->position().x();
        const qreal inDistance = qAbs(x - xForTime(m_in));
        const qreal outDistance = qAbs(x - xForTime(m_out));
        const Drag hover = m_duration > 0 && qMin(inDistance, outDistance) <= 11
            ? (inDistance <= outDistance ? Drag::In : Drag::Out) : Drag::None;
        if (hover != m_hover) {
            m_hover = hover;
            setCursor(hover == Drag::None ? Qt::PointingHandCursor : Qt::SizeHorCursor);
            update();
        }
        return;
    }
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
    update();
    event->accept();
}

void VideoTimeline::leaveEvent(QEvent *event) {
    m_hover = Drag::None;
    update();
    QWidget::leaveEvent(event);
}

}
