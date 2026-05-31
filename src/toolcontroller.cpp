#include "toolcontroller.h"
#include "undocommands.h"
#include "items/arrowitem.h"
#include "items/rectitem.h"
#include "items/ellipseitem.h"
#include "items/highlightitem.h"
#include "items/redactitem.h"
#include "items/penpathitem.h"
#include "items/rasteritem.h"
#include "items/textitem.h"
#include <QGraphicsScene>
#include <QUndoStack>
#include <QVariantAnimation>

namespace eddy {

ToolType toolFromName(const QString &name) {
    const QString n = name.toLower();
    if (n=="move") return ToolType::Move;
    if (n=="arrow") return ToolType::Arrow;
    if (n=="pen") return ToolType::Pen;
    if (n=="rect"||n=="box"||n=="rectangle") return ToolType::Rect;
    if (n=="ellipse"||n=="circle") return ToolType::Ellipse;
    if (n=="highlight") return ToolType::Highlight;
    if (n=="text") return ToolType::Text;
    if (n=="blur") return ToolType::Blur;
    if (n=="pixelate") return ToolType::Pixelate;
    if (n=="redact") return ToolType::Redact;
    return ToolType::Arrow;
}

ToolController::ToolController(QGraphicsScene *scene, QUndoStack *undo, QImage bg, QObject *parent)
    : QObject(parent), m_scene(scene), m_undo(undo), m_bg(std::move(bg)) {}

void ToolController::setTool(ToolType t) {
    if (m_tool == t) return;
    m_tool = t;
    emit toolChanged(t);
}

void ToolController::finalizeFade() {
    if (m_fadeAnim) m_fadeAnim->stop();                 // DeleteWhenStopped frees it; QPointer nulls
    if (m_fadingItem && m_fadingItem->scene())
        m_fadingItem->setOpacity(1.0);                  // snap any in-flight fade fully opaque
    m_fadingItem = nullptr;
}

void ToolController::fadeIn(QGraphicsItem *it) {
    if (!m_animations) { it->setOpacity(1.0); return; }
    it->setOpacity(0.0);
    m_fadingItem = it;
    auto *a = new QVariantAnimation(this);
    a->setStartValue(0.0); a->setEndValue(1.0);
    a->setDuration(130); a->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(a, &QVariantAnimation::valueChanged, this, [it](const QVariant &v){
        if (it->scene()) it->setOpacity(v.toDouble());  // guard: undone mid-fade (item still alive)
    });
    m_fadeAnim = a;
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

template <class T> static void style(T *it, const QColor &c, double w) {
    it->setStrokeColor(c); it->setStrokeWidth(w);
}

void ToolController::begin(const QPointF &p) {
    if (m_active) { m_scene->removeItem(m_active); delete m_active; m_active = nullptr; }
    m_start = p; m_active = nullptr;
    bool isRaster = false;
    switch (m_tool) {
        case ToolType::Arrow: { auto *a = new ArrowItem(p,p); style(a,m_color,m_width); m_active=a; break; }
        case ToolType::Rect: { auto *r = new RectItem(QRectF(p,p)); style(r,m_color,m_width); m_active=r; break; }
        case ToolType::Ellipse: { auto *e = new EllipseItem(QRectF(p,p)); style(e,m_color,m_width); m_active=e; break; }
        case ToolType::Highlight: { auto *h = new HighlightItem(QRectF(p,p)); m_active=h; break; }
        case ToolType::Redact: { auto *r = new RedactItem(QRectF(p,p)); m_active=r; break; }
        case ToolType::Pen: { auto *pp = new PenPathItem(p); style(pp,m_color,m_width); m_active=pp; break; }
        case ToolType::Blur: { auto *b = new RasterItem(RasterItem::Blur, m_bg, QRectF(p,p)); m_active=b; isRaster=true; break; }
        case ToolType::Pixelate: { auto *b = new RasterItem(RasterItem::Pixelate, m_bg, QRectF(p,p)); m_active=b; isRaster=true; break; }
        default: break; // Move/Text handled elsewhere
    }
    if (m_active) {
        if (!isRaster)
            m_active->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        m_scene->addItem(m_active); // live preview; committed in finish()
    }
}

void ToolController::update(const QPointF &p) {
    if (!m_active) return;
    if (auto *a = dynamic_cast<ArrowItem*>(m_active)) a->setEnd(p);
    else if (auto *r = dynamic_cast<RectItem*>(m_active)) r->setRect(QRectF(m_start,p));
    else if (auto *e = dynamic_cast<EllipseItem*>(m_active)) e->setRect(QRectF(m_start,p));
    else if (auto *h = dynamic_cast<HighlightItem*>(m_active)) h->setRect(QRectF(m_start,p));
    else if (auto *rd = dynamic_cast<RedactItem*>(m_active)) rd->setRect(QRectF(m_start,p));
    else if (auto *pp = dynamic_cast<PenPathItem*>(m_active)) pp->addPoint(p);
    else if (auto *ra = dynamic_cast<RasterItem*>(m_active)) ra->setRegion(QRectF(m_start,p));
}

void ToolController::finish(const QPointF &p) {
    if (!m_active) return;
    update(p);
    finalizeFade();                  // settle prior fade before push() can discard the redo branch
    QGraphicsItem *committed = m_active;
    m_scene->removeItem(m_active);
    m_undo->push(new AddItemCommand(m_scene, m_active));
    m_active = nullptr;
    fadeIn(committed);
}

QGraphicsItem *ToolController::placeText(const QPointF &p) {
    auto *t = new TextItem(QString(), m_color, qMax(14.0, m_width * 4.0));
    t->setPos(p);
    t->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    t->setTextInteractionFlags(Qt::TextEditorInteraction);
    finalizeFade();
    m_undo->push(new AddItemCommand(m_scene, t));
    fadeIn(t);
    return t;
}

}
