#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QVector>

namespace eddy {

class VideoTimeline : public QWidget {
    Q_OBJECT
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
    qint64 visibleStart() const { return m_viewStart; }
    qint64 visibleEnd() const { return m_viewEnd; }
    QVector<qint64> thumbnailTimes() const;
    bool interacting() const { return m_drag != Drag::None; }
    bool trimming() const { return m_drag == Drag::In || m_drag == Drag::Out; }

    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }
    qint64 trimIn() const { return m_in; }
    qint64 trimOut() const { return m_out; }
    bool hasContactSheet() const { return !m_contactSheet.isNull(); }
    int contactSheetFrameCount() const { return m_contactSheetFrames; }

signals:
    void seekRequested(qint64 positionMs);
    void trimPreviewed(qint64 inMs, qint64 outMs);
    void trimCommitted(qint64 inMs, qint64 outMs);
    void interactionStarted(bool trimming);
    void interactionFinished(bool cancelled);
    void viewRangeChanged();
    void hoverRequested(qint64 timeMs, QPoint position);
    void hoverLeft();

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
    enum class Drag { None, In, Out, Seek };
    QRectF trackRect() const;
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
};

}
