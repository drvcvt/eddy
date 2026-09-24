#pragma once
#include "annotationitem.h"
#include <QFont>
#include <QGraphicsScene>

namespace eddy {

// A numbered step (21.09. plan 6): a filled circle in the annotation colour
// with its number in the middle. The item's position is the circle's centre;
// more digits widen the circle instead of shrinking the type.
class StepItem : public AnnotationItem {
public:
    enum class Size { S, M, L };
    static constexpr int kMaxNumber = 999;

    explicit StepItem(int number, Size size = Size::M);
    int number() const { return m_number; }
    void setNumber(int number);
    Size size() const { return m_size; }
    void setSize(Size size);
    qreal diameter() const { return m_diameter; }

    StepItem *clone() const override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

    // One more than the highest step in `scene`, 1 when there is none.
    static int nextNumber(const QGraphicsScene *scene);
    // Rises with every step made, copies and loaded ones included: the order
    // "Renumber by creation order" follows, whatever undo did to the stacking.
    quint64 serial() const { return m_serial; }

private:
    QFont font() const;
    void measure();
    int m_number;
    Size m_size;
    qreal m_diameter = 0;   // measured once per number and size, not per paint
    quint64 m_serial;
};

}
