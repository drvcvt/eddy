#pragma once
#include <QWidget>
#include <QImage>

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

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum class Drag { None, In, Out, Seek };
    QRectF trackRect() const;
    qreal xForTime(qint64 timeMs) const;
    qint64 timeForX(qreal x) const;

    qint64 m_duration = 0;
    qint64 m_position = 0;
    qint64 m_in = 0;
    qint64 m_out = 0;
    qint64 m_minimumRange = 1;
    Drag m_drag = Drag::None;
    QImage m_contactSheet;
    int m_contactSheetFrames = 0;
};

}
