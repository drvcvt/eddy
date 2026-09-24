#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QVector>
#include <functional>
#include "studiodocument.h"
#include "timemap.h"

namespace eddy {

class VideoTimeline : public QWidget {
    Q_OBJECT
    enum class Drag { None, In, Out, Seek, ZoomMove, ZoomStart, ZoomEnd };
public:
    explicit VideoTimeline(QWidget *parent = nullptr);

    void setDuration(qint64 durationMs);
    void setMinimumRange(qint64 durationMs);
    void setPosition(qint64 positionMs);
    void setTrimRange(qint64 inMs, qint64 outMs);
    void setContactSheet(const QImage &image, int frameCount);
    void setThumbnail(qint64 timeMs, const QImage &image);
    void zoomAt(qreal factor, qint64 anchorMs);
    void panBy(qint64 deltaMs);
    void fitClip();
    void cancelInteraction();
    // Source times at the view's edges.
    qint64 visibleStart() const { return source(m_viewStart); }
    qint64 visibleEnd() const { return source(m_viewEnd); }
    // Pans so `sourceMs` is in view.
    void ensureVisible(qint64 sourceMs);
    // Fragments (studio plan 6.6, Q5 = B): the timeline shows the edited
    // time, cuts collapse to a notch in the ruler and 2x takes half the width.
    // Every public time stays source time.
    void setFragments(const QVector<Fragment> &fragments);
    void setSelectedFragment(int index);   // -1: none
    // Ruler ticks as (x, edited ms).
    QVector<QPair<qreal, qint64>> rulerTicks() const;
    QVector<qint64> thumbnailTimes() const;
    QImage thumbnailNear(qint64 time, qint64 *sampleTime) const;
    bool interacting() const { return m_drag != Drag::None; }
    bool trimming() const { return m_drag == Drag::In || m_drag == Drag::Out; }

    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }
    qint64 trimIn() const { return m_in; }
    qint64 trimOut() const { return m_out; }
    bool hasContactSheet() const { return !m_contactSheet.isNull(); }
    int contactSheetFrameCount() const { return m_contactSheetFrames; }

    // Zoom lane (studio plan 6.1, Q1 = C): a 28 px row under the film strip on
    // the same time axis, shown while Studio is on or zooms exist.
    void setZoomLaneVisible(bool visible);
    bool zoomLaneVisible() const { return m_laneVisible; }
    // `level` is the camera's zoom at a source time, 1 when it shows everything.
    void setZooms(const QVector<ZoomSegment> &zooms, std::function<double(qint64)> level = {});
    QVector<ZoomSegment> zooms() const { return m_zooms; }
    void setSelectedZoom(quint32 id);   // 0: none
    quint32 selectedZoom() const { return m_selectedZoom; }
    QRectF zoomLaneRect() const;
    bool zoomDragging() const {
        return m_drag == Drag::ZoomMove || m_drag == Drag::ZoomStart || m_drag == Drag::ZoomEnd;
    }

signals:
    void seekRequested(qint64 positionMs);
    void trimPreviewed(qint64 inMs, qint64 outMs);
    void trimCommitted(qint64 inMs, qint64 outMs);
    void interactionStarted(bool trimming);
    void interactionFinished(bool cancelled);
    void viewRangeChanged();
    void hoverRequested(qint64 timeMs, QPoint position);
    void hoverLeft();
    void zoomAddRequested(qint64 timeMs);
    void zoomSelected(quint32 id);
    void zoomsPreviewed(const QVector<ZoomSegment> &zooms);
    void zoomsEdited(const QVector<ZoomSegment> &before, const QVector<ZoomSegment> &after);
    void zoomMenuRequested(quint32 id, QPoint globalPos);
    void cutClicked(int fragment);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    QRectF trackRect() const;
    quint32 zoomAt(QPointF pos, Drag *part) const;
    int cutAt(QPointF pos) const;
    double edited(qint64 sourceMs) const;
    qint64 source(double editedMs) const;
    qint64 editedDuration() const;
    void rebuildAxis();
    void moveZoomDrag(qreal x);
    qreal xForTime(qint64 timeMs) const;
    qint64 timeForX(qreal x) const;
    void setViewRange(qint64 startMs, qint64 endMs);
    void moveDrag(qreal x, Qt::KeyboardModifiers modifiers);

    qint64 m_duration = 0;
    qint64 m_position = 0;
    qint64 m_in = 0;
    qint64 m_out = 0;
    qint64 m_minimumRange = 1;
    qint64 m_viewStart = 0, m_viewEnd = 0;
    qint64 m_beforeIn = 0, m_beforeOut = 0, m_beforePosition = 0;
    qreal m_dragX = 0, m_dragTime = 0;
    bool m_fine = false;
    qreal m_pointerX = 0;
    Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
    QTimer m_edgePan;
    QHash<qint64, QImage> m_thumbnails;
    Drag m_drag = Drag::None;
    Drag m_hover = Drag::None;
    QImage m_contactSheet;
    int m_contactSheetFrames = 0;
    bool m_laneVisible = false;
    QVector<ZoomSegment> m_zooms, m_zoomsBefore;
    std::function<double(qint64)> m_zoomLevel;
    quint32 m_selectedZoom = 0, m_dragZoom = 0;
    qint64 m_grabOffset = 0;
    bool m_zoomMoved = false;
    QVector<Fragment> m_fragments;
    TimeMap m_axis;                 // source -> edited time, without the trim
    int m_selectedFragment = -1;
    int m_hoverCut = -1;
};

}
