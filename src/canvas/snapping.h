#pragma once
#include <QLineF>
#include <QRectF>
#include <QVector>

class QGraphicsItem;

namespace eddy {

// The box alignment and snapping work with, in scene coordinates: the shape
// without its stroke padding; a spotlight's focus, not its dimmed canvas.
QRectF alignmentBounds(const QGraphicsItem *item);

// Guides and snapping while dragging (21.09. plan 6): the moving box's
// left, centre and right (top, middle, bottom) catch the same lines of the
// targets within `catchDistance` and let go beyond `releaseDistance`, so a
// snap does not flicker. Each call works from the raw, unsnapped box.
class Snapper {
public:
    QPointF snap(const QRectF &moving, const QVector<QRectF> &targets, qreal catchDistance, qreal releaseDistance);
    QVector<QLineF> guides() const { return m_guides; }
    void reset();

private:
    struct Lock {
        bool on = false;
        int anchor = 0;   // 0 start, 1 centre, 2 end of the moving box
        qreal value = 0;
        QRectF target;
    };
    static qreal axis(Lock &lock, qreal a0, qreal a1, qreal a2, const QVector<QRectF> &targets, bool horizontal,
                      qreal catchDistance, qreal releaseDistance);
    Lock m_x, m_y;
    QVector<QLineF> m_guides;
};

enum class Align { Left, HCenter, Right, Top, VCenter, Bottom };

// Moves that line the boxes up on the edge or centre of their common bounds.
QVector<QPointF> alignDeltas(const QVector<QRectF> &boxes, Align align);
// Moves that make the visible gaps between boxes equal along one axis; the
// outermost stay put. Empty when there are fewer than three or no room.
QVector<QPointF> distributeDeltas(const QVector<QRectF> &boxes, Qt::Orientation orientation);

}
