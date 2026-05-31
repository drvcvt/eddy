#include "selectionhandles.h"
#include "items/annotationitem.h"
#include "items/arrowitem.h"
#include "undocommands.h"
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>
#include <QBrush>
#include <QPen>
#include <QCursor>

namespace eddy {

// One draggable handle. role 0..7 = TL,T,TR,R,BR,B,BL,L for rects; 0/1 = start/end for arrows.
class HandleItem : public QGraphicsRectItem {
public:
    HandleItem(SelectionHandles *owner, QGraphicsItem *target, int role, QUndoStack *undo)
        : QGraphicsRectItem(-5, -5, 10, 10), m_owner(owner), m_target(target),
          m_role(role), m_undo(undo) {
        setBrush(QBrush(QColor("#cccccc")));
        setPen(QPen(QColor("#1a1a1a"), 1));
        setZValue(10000);
        setFlag(ItemIgnoresTransformations, true);   // constant on-screen size
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(QCursor(Qt::SizeAllCursor));
    }
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override { captureBefore(); e->accept(); }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override {
        applyResize(e->scenePos());
        if (m_owner) m_owner->reposition();          // move handles, do NOT rebuild (no self-delete)
        e->accept();
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override { pushCommand(); e->accept(); }
private:
    SelectionHandles *m_owner; QGraphicsItem *m_target; int m_role; QUndoStack *m_undo;
    QRectF m_beforeRect, m_afterRect;
    QPointF m_s0, m_e0, m_s1, m_e1;

    QPointF local(const QPointF &scenePos) const { return m_target->mapFromScene(scenePos); }

    void captureBefore() {
        // Seed "after" == "before" so a press+release without any move is a no-op
        // (otherwise pushCommand would resize to a default-null rect / origin point).
        if (auto *a = dynamic_cast<ArrowItem*>(m_target)) {
            m_s0 = a->start(); m_e0 = a->end(); m_s1 = m_s0; m_e1 = m_e0;
        } else if (auto *an = dynamic_cast<AnnotationItem*>(m_target)) {
            m_beforeRect = an->rect(); m_afterRect = m_beforeRect;
        }
    }
    void applyResize(const QPointF &scenePos) {
        const QPointF p = local(scenePos);
        if (auto *a = dynamic_cast<ArrowItem*>(m_target)) {
            if (m_role == 0) a->setStart(p); else a->setEnd(p);
            m_s1 = a->start(); m_e1 = a->end();
            return;
        }
        auto *an = dynamic_cast<AnnotationItem*>(m_target);
        if (!an) return;
        QRectF rc = an->rect();
        switch (m_role) {
            case 0: rc.setTopLeft(p); break;      case 1: rc.setTop(p.y()); break;
            case 2: rc.setTopRight(p); break;     case 3: rc.setRight(p.x()); break;
            case 4: rc.setBottomRight(p); break;  case 5: rc.setBottom(p.y()); break;
            case 6: rc.setBottomLeft(p); break;   case 7: rc.setLeft(p.x()); break;
        }
        an->setRect(rc.normalized());
        m_afterRect = rc.normalized();
    }
    void pushCommand() {
        if (!m_undo) return;
        if (auto *a = dynamic_cast<ArrowItem*>(m_target)) {
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
}

int SelectionHandles::handleCount() const { return m_handles.size(); }

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
    auto *an = dynamic_cast<AnnotationItem*>(m_target);
    if (!an || m_handles.size() < 8) return;
    QPointF pts[8]; rectCorners(an->rect(), pts);
    for (int i = 0; i < 8; ++i) m_handles[i]->setPos(m_target->mapToScene(pts[i]));
}

}
