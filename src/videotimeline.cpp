#include "videotimeline.h"
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
    setFixedHeight(52);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Video timeline"));
    setToolTip(QStringLiteral("Drag to seek · ends to trim · Shift for fine trim · Ctrl+wheel to zoom"));
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
        QMenu menu(this);
        menu.addAction(tr("Zoom in · +"), this, [this] { zoomAt(2, m_position); });
        menu.addAction(tr("Zoom out · −"), this, [this] { zoomAt(0.5, m_position); });
        menu.addAction(tr("Fit clip · 0"), this, &VideoTimeline::fitClip);
        menu.exec(mapToGlobal(pos));
    });
}

void VideoTimeline::setViewRange(qint64 startMs, qint64 endMs) {
    const qint64 span = qBound<qint64>(qMin(m_duration, m_minimumRange * 10),
                                     endMs - startMs, m_duration);
    const qint64 start = qBound<qint64>(0, startMs, m_duration - span);
    if (m_viewStart == start && m_viewEnd == start + span) return;
    m_viewStart = start;
    m_viewEnd = start + span;
    m_thumbnails.clear();
    emit viewRangeChanged();
    update();
}

void VideoTimeline::zoomAt(qreal factor, qint64 anchorMs) {
    if (m_duration <= 0 || !std::isfinite(factor) || factor <= 0) return;
    anchorMs = qBound(m_viewStart, anchorMs, m_viewEnd);
    const qint64 oldSpan = m_viewEnd - m_viewStart;
    const qint64 span = qBound(qMin(m_duration, m_minimumRange * 10),
                              qRound64(oldSpan / factor), m_duration);
    const qreal fraction = oldSpan > 0 ? qreal(anchorMs - m_viewStart) / oldSpan : 0.5;
    const qint64 start = anchorMs - qRound64(fraction * span);
    setViewRange(start, start + span);
}

void VideoTimeline::panBy(qint64 deltaMs) {
    setViewRange(m_viewStart + deltaMs, m_viewEnd + deltaMs);
}

void VideoTimeline::fitClip() { setViewRange(0, m_duration); }

QVector<qint64> VideoTimeline::thumbnailTimes() const {
    QVector<qint64> times;
    if (m_duration <= 0) return times;
    const int count = qBound(6, width() / 64, 24);
    for (int i = 0; i < count; ++i)
        times.append(qMin(m_duration - 1, m_viewStart
            + qRound64((i + 0.5) * (m_viewEnd - m_viewStart) / count)));
    return times;
}

void VideoTimeline::setThumbnail(qint64 timeMs, const QImage &image) {
    if (!image.isNull() && thumbnailTimes().contains(timeMs)) {
        m_thumbnails.insert(timeMs, image);
        update();
    }
}

QImage VideoTimeline::thumbnailNear(qint64 time, qint64 *sampleTime) const {
    auto nearest = m_thumbnails.cend();
    for (auto it = m_thumbnails.cbegin(); it != m_thumbnails.cend(); ++it)
        if (nearest == m_thumbnails.cend() || qAbs(it.key() - time) < qAbs(nearest.key() - time)) nearest = it;
    if (nearest == m_thumbnails.cend()) return {};
    if (sampleTime) *sampleTime = nearest.key();
    return nearest.value();
}

void VideoTimeline::setContactSheet(const QImage &image, int frameCount) {
    m_contactSheet = image;
    m_contactSheetFrames = image.isNull() ? 0 : qMax(1, frameCount);
    update();
}

void VideoTimeline::setDuration(qint64 durationMs) {
    const bool fullView = m_viewStart == 0 && m_viewEnd == m_duration;
    m_duration = qMax<qint64>(0, durationMs);
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
    return QRectF(6, 18, qMax(1, width() - 12), height() - 22);
}

qreal VideoTimeline::xForTime(qint64 timeMs) const {
    const QRectF track = trackRect();
    if (m_viewEnd <= m_viewStart) return track.left();
    return track.left() + track.width() * qreal(timeMs - m_viewStart) / (m_viewEnd - m_viewStart);
}

qint64 VideoTimeline::timeForX(qreal x) const {
    const QRectF track = trackRect();
    if (m_duration <= 0 || track.width() <= 0) return 0;
    const qreal fraction = qBound<qreal>(0, (x - track.left()) / track.width(), 1);
    return m_viewStart + qRound64(fraction * (m_viewEnd - m_viewStart));
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
    const auto times = thumbnailTimes();
    for (int i = 0; i < times.size(); ++i) {
        const auto it = m_thumbnails.constFind(times[i]);
        if (it == m_thumbnails.cend()) continue;
        const QRectF slot(track.left() + i * track.width() / times.size(), track.top(),
                          track.width() / times.size(), track.height());
        const qreal scale = qMax(slot.width() / it->width(), slot.height() / it->height());
        const QSizeF crop(slot.width() / scale, slot.height() / scale);
        const QRectF source((it->width() - crop.width()) / 2,
                            (it->height() - crop.height()) / 2, crop.width(), crop.height());
        painter.drawImage(slot, *it, source);
    }
    QColor shade = background;
    shade.setAlpha(205);
    painter.fillRect(QRectF(track.left(), track.top(), qMax(0.0, inX - track.left()), track.height()), shade);
    painter.fillRect(QRectF(qMax(track.left(), outX), track.top(),
                           qMax(0.0, track.right() - outX), track.height()), shade);
    painter.restore();

    const qreal playX = xForTime(m_position);
    painter.setBrush(ink(0.86));
    if (playX >= track.left() && playX <= track.right()) {
        painter.drawRoundedRect(QRectF(playX - 1, track.top(), 2, track.height() + 3), 1, 1);
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

    QFont boundaryFont = font(); boundaryFont.setPixelSize(10);
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
    QFont ruler = font(); ruler.setPixelSize(9); painter.setFont(ruler);
    const qreal rawStep = qMax<qreal>(1, (m_viewEnd - m_viewStart) * 70.0 / track.width());
    const qreal base = std::pow(10.0, std::floor(std::log10(rawStep)));
    const qint64 step = qMax<qint64>(1, qRound64(base * (rawStep / base <= 2 ? 2 : rawStep / base <= 5 ? 5 : 10)));
    for (qint64 t = (m_viewStart / step + 1) * step; t < m_viewEnd; t += step) {
        const qreal x = xForTime(t);
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
    if (hasFocus()) {
        painter.setPen(QPen(ink(0.4), 1)); painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(-2, -2, 2, 2), 7, 7);
    }
}

void VideoTimeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_duration <= 0) return;
    setFocus(Qt::MouseFocusReason);
    const qreal x = event->position().x();
    const qreal inDistance = m_in < m_viewStart || m_in > m_viewEnd ? width() + 20 : qAbs(x - xForTime(m_in));
    const qreal outDistance = m_out < m_viewStart || m_out > m_viewEnd ? width() + 20 : qAbs(x - xForTime(m_out));
    if (qMin(inDistance, outDistance) <= 11)
        m_drag = inDistance <= outDistance ? Drag::In : Drag::Out;
    else
        m_drag = Drag::Seek;
    m_beforeIn = m_in; m_beforeOut = m_out; m_beforePosition = m_position;
    m_dragX = x;
    m_dragTime = m_drag == Drag::In ? m_in : m_out;
    m_fine = event->modifiers().testFlag(Qt::ShiftModifier);
    emit interactionStarted(trimming());
    m_edgePan.start();
    mouseMoveEvent(event);
    event->accept();
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *event) {
    if (m_drag == Drag::None) {
        const qreal x = event->position().x();
        const qreal inDistance = m_in < m_viewStart || m_in > m_viewEnd ? width() + 20 : qAbs(x - xForTime(m_in));
        const qreal outDistance = m_out < m_viewStart || m_out > m_viewEnd ? width() + 20 : qAbs(x - xForTime(m_out));
        const Drag hover = m_duration > 0 && qMin(inDistance, outDistance) <= 11
            ? (inDistance <= outDistance ? Drag::In : Drag::Out) : Drag::None;
        if (hover != m_hover) {
            m_hover = hover;
            setCursor(hover == Drag::None ? Qt::PointingHandCursor : Qt::SizeHorCursor);
            update();
        }
        emit hoverRequested(timeForX(x), event->pos());
        return;
    }
    moveDrag(event->position().x(), event->modifiers());
    event->accept();
}

void VideoTimeline::moveDrag(qreal x, Qt::KeyboardModifiers modifiers) {
    m_pointerX = x;
    m_modifiers = modifiers;
    const bool fine = modifiers.testFlag(Qt::ShiftModifier);
    if (fine != m_fine) {
        m_dragTime = m_drag == Drag::In ? m_in : m_out;
        m_dragX = x;
        m_fine = fine;
    }
    const qint64 time = m_drag == Drag::Seek ? timeForX(x)
        : qRound64(m_dragTime + (x - m_dragX) * (m_viewEnd - m_viewStart)
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
    if (trimming()) emit hoverRequested(m_position, QPoint(qRound(x), 18));
    update();
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_drag == Drag::None) return;
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
    if (!interacting()) emit hoverLeft();
    setCursor(Qt::PointingHandCursor);
    update();
    QWidget::leaveEvent(event);
}

void VideoTimeline::cancelInteraction() {
    if (!interacting()) return;
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
    m_thumbnails.clear();
    emit viewRangeChanged();
}

void VideoTimeline::focusOutEvent(QFocusEvent *event) {
    cancelInteraction();
    emit hoverLeft();
    QWidget::focusOutEvent(event);
}

}
