#include "selectionhandles.h"
#include "items/annotationitem.h"
#include "items/arrowitem.h"
#include "items/textitem.h"
#include "undocommands.h"
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>
#include <QBrush>
#include <QPen>
#include <QCursor>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QApplication>
#include <cmath>
#include <numbers>

namespace eddy {

QPointF snappedArrowEnd(const QPointF &fixed, const QPointF &pointer,
                       Qt::KeyboardModifiers modifiers) {
    if (!modifiers.testFlag(Qt::ShiftModifier)) return pointer;
    const QPointF delta = pointer - fixed;
    const qreal length = std::hypot(delta.x(), delta.y());
    constexpr qreal step = std::numbers::pi_v<qreal> / 4.0;
    const qreal angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
    return fixed + QPointF(length * std::cos(angle), length * std::sin(angle));
}

static QSizeF aspectSize(qreal width, qreal height, qreal ratio) {
    width = qAbs(width);
    height = qAbs(height);
    const qreal fromWidth = width / ratio;
    const qreal fromHeight = height * ratio;
    return qAbs(fromWidth - height) <= qAbs(fromHeight - width)
        ? QSizeF(width, fromWidth)
        : QSizeF(fromHeight, height);
}

QRectF resizedRect(const QRectF &before, int role, const QPointF &pointer,
                   Qt::KeyboardModifiers modifiers) {
    const QRectF original = before.normalized();
    const QPointF center = original.center();
    const qreal ratio = original.height() > 0
        ? original.width() / original.height()
        : 1.0;
    const bool keepAspect = modifiers.testFlag(Qt::ShiftModifier);
    const bool fromCenter = modifiers.testFlag(Qt::AltModifier);

    if ((role % 2) == 0) {
        if (fromCenter) {
            QSizeF half(qAbs(pointer.x() - center.x()), qAbs(pointer.y() - center.y()));
            if (keepAspect) half = aspectSize(half.width(), half.height(), ratio);
            return QRectF(center - QPointF(half.width(), half.height()),
                          center + QPointF(half.width(), half.height())).normalized();
        }
        const QPointF fixed[] = {
            original.bottomRight(), {}, original.bottomLeft(), {},
            original.topLeft(), {}, original.topRight(), {}
        };
        const QPointF anchor = fixed[role];
        QPointF delta = pointer - anchor;
        if (keepAspect) {
            const QSizeF size = aspectSize(delta.x(), delta.y(), ratio);
            delta = {std::copysign(size.width(), delta.x()),
                     std::copysign(size.height(), delta.y())};
        }
        return QRectF(anchor, anchor + delta).normalized();
    }

    QRectF result = original;
    if (role == 3 || role == 7) {
        if (fromCenter) {
            const qreal halfWidth = qAbs(pointer.x() - center.x());
            result.setLeft(center.x() - halfWidth);
            result.setRight(center.x() + halfWidth);
        } else if (role == 3) {
            result.setRight(pointer.x());
        } else {
            result.setLeft(pointer.x());
        }
        if (keepAspect) {
            const qreal halfHeight = qAbs(result.width() / ratio) / 2.0;
            result.setTop(center.y() - halfHeight);
            result.setBottom(center.y() + halfHeight);
        }
    } else {
        if (fromCenter) {
            const qreal halfHeight = qAbs(pointer.y() - center.y());
            result.setTop(center.y() - halfHeight);
            result.setBottom(center.y() + halfHeight);
        } else if (role == 5) {
            result.setBottom(pointer.y());
        } else {
            result.setTop(pointer.y());
        }
        if (keepAspect) {
            const qreal halfWidth = qAbs(result.height() * ratio) / 2.0;
            result.setLeft(center.x() - halfWidth);
            result.setRight(center.x() + halfWidth);
        }
    }
    return result.normalized();
}

static QCursor cursorForRole(int role) {
    if (role == 8) return QCursor(Qt::SizeHorCursor);
    switch (role) {
        case 0: case 4: return QCursor(Qt::SizeFDiagCursor);  // TL / BR
        case 2: case 6: return QCursor(Qt::SizeBDiagCursor);  // TR / BL
        case 1: case 5: return QCursor(Qt::SizeVerCursor);    // T / B
        case 3: case 7: return QCursor(Qt::SizeHorCursor);    // L / R
        default:        return QCursor(Qt::SizeAllCursor);    // arrow endpoints
    }
}

// One draggable handle. role 0..7 = TL,T,TR,R,BR,B,BL,L for rects; 0/1 = start/end for arrows.
class HandleItem : public QGraphicsRectItem {
public:
    HandleItem(SelectionHandles *owner, QGraphicsItem *target, int role, QUndoStack *undo)
        : QGraphicsRectItem(-6, -6, 12, 12), m_owner(owner), m_target(target),
          m_role(role), m_undo(undo) {
        // 12x12 bounds with a 10x10 visible, constant-size squircle grip.
        setZValue(10000);
        setFlag(ItemIgnoresTransformations, true);   // constant on-screen size
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(cursorForRole(role));
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF face = rect().adjusted(1, 1, -1, -1);
        p->setPen(Qt::NoPen);
        p->setBrush(QApplication::palette().color(QPalette::Highlight));
        p->drawRoundedRect(face, 4, 4);
    }
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override { captureBefore(); e->accept(); }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override {
        applyResize(e->scenePos(), e->modifiers());
        if (m_owner) m_owner->reposition();          // move handles, do NOT rebuild (no self-delete)
        e->accept();
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override {
        pushCommand();
        if (m_owner) emit m_owner->resizeFinished(m_target);   // HandleItem is a friend
        e->accept();
    }
private:
    SelectionHandles *m_owner; QGraphicsItem *m_target; int m_role; QUndoStack *m_undo;
    QRectF m_beforeRect, m_afterRect;
    QPointF m_s0, m_e0, m_s1, m_e1;
    TextState m_textBefore, m_textAfter;

    QPointF local(const QPointF &scenePos) const { return m_target->mapFromScene(scenePos); }

    void captureBefore() {
        // Seed "after" == "before" so a press+release without any move is a no-op
        // (otherwise pushCommand would resize to a default-null rect / origin point).
        if (auto *a = dynamic_cast<ArrowItem*>(m_target)) {
            m_s0 = a->start(); m_e0 = a->end(); m_s1 = m_s0; m_e1 = m_e0;
        } else if (auto *text = dynamic_cast<TextItem *>(m_target)) {
            m_textBefore = text->state(); m_textAfter = m_textBefore;
        } else if (auto *an = dynamic_cast<AnnotationItem*>(m_target)) {
            m_beforeRect = an->rect(); m_afterRect = m_beforeRect;
        }
    }
    void applyResize(const QPointF &scenePos, Qt::KeyboardModifiers modifiers) {
        const QPointF p = local(scenePos);
        if (auto *text = dynamic_cast<TextItem *>(m_target)) {
            text->setTextWidth(qMax<qreal>(40.0, p.x() - text->boundingRect().left()));
            m_textAfter = text->state();
            return;
        }
        if (auto *a = dynamic_cast<ArrowItem*>(m_target)) {
            if (m_role == 0) {
                a->setStart(snappedArrowEnd(m_e0, p, modifiers));
                a->setEnd(m_e0);
            } else {
                a->setStart(m_s0);
                a->setEnd(snappedArrowEnd(m_s0, p, modifiers));
            }
            m_s1 = a->start(); m_e1 = a->end();
            return;
        }
        auto *an = dynamic_cast<AnnotationItem*>(m_target);
        if (!an) return;
        m_afterRect = resizedRect(m_beforeRect, m_role, p, modifiers);
        an->setRect(m_afterRect);
    }
    void pushCommand() {
        if (!m_undo) return;
        if (auto *text = dynamic_cast<TextItem *>(m_target)) {
            if (!(m_textAfter == m_textBefore))
                m_undo->push(new EditTextCommand(text, m_textBefore, m_textAfter));
        } else if (auto *a = dynamic_cast<ArrowItem*>(m_target)) {
            if (m_s1 == m_s0 && m_e1 == m_e0) return;          // unchanged: no spurious undo entry
            m_undo->push(new ResizeArrowCommand(a, m_s0, m_e0, m_s1, m_e1));
        } else if (auto *an = dynamic_cast<AnnotationItem*>(m_target)) {
            if (m_afterRect == m_beforeRect) return;
            m_undo->push(new ResizeRectCommand(an, m_beforeRect, m_afterRect));  // rect/ellipse/highlight/redact
        }
    }
};

SelectionHandles::SelectionHandles(QGraphicsScene *scene, QUndoStack *undo, QObject *parent)
    : QObject(parent), m_scene(scene), m_undo(undo) {
    connect(scene, &QGraphicsScene::selectionChanged, this, &SelectionHandles::refresh);
    connect(scene, &QGraphicsScene::changed, this, &SelectionHandles::reposition);
}

int SelectionHandles::handleCount() const { return m_handles.size(); }

QPointF SelectionHandles::handleScenePos(int i) const {
    if (i < 0 || i >= m_handles.size()) return {};
    return m_handles[i]->scenePos();
}

void SelectionHandles::clear() {
    for (auto *h : m_handles) { m_scene->removeItem(h); delete h; }
    m_handles.clear();
    m_target = nullptr;
}

static void rectCorners(const QRectF &rc, QPointF out[8]) {
    out[0] = rc.topLeft();                       out[1] = {rc.center().x(), rc.top()};
    out[2] = rc.topRight();                      out[3] = {rc.right(), rc.center().y()};
    out[4] = rc.bottomRight();                   out[5] = {rc.center().x(), rc.bottom()};
    out[6] = rc.bottomLeft();                    out[7] = {rc.left(), rc.center().y()};
}

void SelectionHandles::refresh() {
    clear();
    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return;
    QGraphicsItem *it = sel.first();
    m_target = it;
    auto add = [&](int role, const QPointF &scenePos){
        auto *h = new HandleItem(this, it, role, m_undo);
        m_scene->addItem(h);
        h->setPos(scenePos);
        m_handles.push_back(h);
    };
    if (auto *a = dynamic_cast<ArrowItem*>(it)) {
        add(0, it->mapToScene(a->start()));
        add(1, it->mapToScene(a->end()));
        return;
    }
    if (auto *text = dynamic_cast<TextItem *>(it)) {
        const QRectF bounds = text->boundingRect();
        add(8, text->mapToScene(QPointF(bounds.right(), bounds.center().y())));
        return;
    }
    auto *an = dynamic_cast<AnnotationItem*>(it);
    if (!an || an->rect().isNull()) { m_target = nullptr; return; }   // pen/text/raster: no handles
    QPointF pts[8]; rectCorners(an->rect(), pts);
    for (int i = 0; i < 8; ++i) add(i, it->mapToScene(pts[i]));
}

void SelectionHandles::reposition() {
    if (!m_target || m_handles.isEmpty()) return;
    if (auto *a = dynamic_cast<ArrowItem*>(m_target)) {
        if (m_handles.size() >= 2) {
            m_handles[0]->setPos(m_target->mapToScene(a->start()));
            m_handles[1]->setPos(m_target->mapToScene(a->end()));
        }
        return;
    }
    if (auto *text = dynamic_cast<TextItem *>(m_target)) {
        const QRectF bounds = text->boundingRect();
        m_handles[0]->setPos(text->mapToScene(QPointF(bounds.right(), bounds.center().y())));
        return;
    }
    auto *an = dynamic_cast<AnnotationItem*>(m_target);
    if (!an || m_handles.size() < 8) return;
    QPointF pts[8]; rectCorners(an->rect(), pts);
    for (int i = 0; i < 8; ++i) m_handles[i]->setPos(m_target->mapToScene(pts[i]));
}

}
