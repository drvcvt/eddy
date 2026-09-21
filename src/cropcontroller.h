#pragma once
#include <QObject>
#include <QRectF>
#include <QVector>

namespace eddy {
// Draft geometry stays in source coordinates; only output is rounded to pixels.
class CropController : public QObject {
    Q_OBJECT
public:
    explicit CropController(QObject *parent = nullptr) : QObject(parent) {}
    void begin(QSize bounds, QRect initial = {}, int grid = 1);
    bool active() const { return m_active; }
    bool dragging() const { return m_role != -1; }
    QRectF rect() const { return m_rect; }
    QRect pixels() const;
    QVector<QPointF> handles() const;
    int hit(QPointF point, qreal tolerance) const;
    Qt::CursorShape cursor(QPointF point, qreal tolerance) const;
    void press(QPointF point, qreal tolerance, Qt::KeyboardModifiers modifiers);
    void move(QPointF point, Qt::KeyboardModifiers modifiers);
    void release();
    void cancelDrag();
    void nudge(QPointF delta);
    void setRatio(qreal ratio);
    void reset();
    void accept();
    void cancel();
signals:
    void changed();
    void accepted(QRect rect);
    void cancelled();
private:
    void assign(QRectF rect);
    QRectF contained(QRectF rect, QPointF anchor, bool preserveAspect) const;
    QRectF m_bounds, m_rect, m_before, m_dragRect;
    QPointF m_pointer;
    Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
    qreal m_ratio = 0;
    int m_grid = 1, m_role = -1;
    bool m_active = false;
};
}
