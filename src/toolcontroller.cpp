#include "toolcontroller.h"
#include "undocommands.h"
#include "items/arrowitem.h"
#include "items/rectitem.h"
#include "items/ellipseitem.h"
#include "items/highlightitem.h"
#include "items/redactitem.h"
#include "items/penpathitem.h"
#include "items/textitem.h"
#include "items/spotlightitem.h"
#include <QGraphicsScene>
#include <QFont>
#include <QUndoStack>
#include <QVariantAnimation>
#include <cmath>
#include <numbers>

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
    if (n=="blur"||n=="pixelate") return ToolType::Redact;   // legacy aliases -> unified redact
    if (n=="redact") return ToolType::Redact;
    if (n=="spotlight") return ToolType::Spotlight;
    return ToolType::Arrow;
}

ToolController::ToolController(QGraphicsScene *scene, QUndoStack *undo, QImage bg, QObject *parent)
    : QObject(parent), m_scene(scene), m_undo(undo), m_bg(std::move(bg)) {
    connect(scene, &QGraphicsScene::focusItemChanged, this,
            [this](QGraphicsItem *now, QGraphicsItem *before, Qt::FocusReason){
        if (m_editingText && before == m_editingText && now != m_editingText)
            commitTextEdit();
    });
}

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

static QPointF constrainedEnd(const QPointF &start, QPointF end,
                              Qt::KeyboardModifiers modifiers, bool arrow) {
    QPointF delta = end - start;
    if (!modifiers.testFlag(Qt::ShiftModifier)) return end;
    if (arrow) {
        const qreal length = std::hypot(delta.x(), delta.y());
        constexpr qreal step = std::numbers::pi_v<qreal> / 4.0;
        const qreal angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
        return start + QPointF(length * std::cos(angle), length * std::sin(angle));
    }
    const qreal side = qMax(qAbs(delta.x()), qAbs(delta.y()));
    delta.setX(std::copysign(side, delta.x()));
    delta.setY(std::copysign(side, delta.y()));
    return start + delta;
}

static QRectF creationRect(const QPointF &start, QPointF end,
                           Qt::KeyboardModifiers modifiers) {
    end = constrainedEnd(start, end, modifiers, false);
    const QPointF delta = end - start;
    return (modifiers.testFlag(Qt::AltModifier)
        ? QRectF(start - delta, start + delta)
        : QRectF(start, end)).normalized();
}

void ToolController::begin(const QPointF &p) {
    if (m_active) { m_scene->removeItem(m_active); delete m_active; m_active = nullptr; }
    m_start = p; m_active = nullptr;
    switch (m_tool) {
        case ToolType::Arrow: { auto *a = new ArrowItem(p,p); style(a,m_color,m_width); m_active=a; break; }
        case ToolType::Rect: { auto *r = new RectItem(QRectF(p,p)); style(r,m_color,m_width); m_active=r; break; }
        case ToolType::Ellipse: { auto *e = new EllipseItem(QRectF(p,p)); style(e,m_color,m_width); m_active=e; break; }
        case ToolType::Highlight: { auto *h = new HighlightItem(QRectF(p,p)); m_active=h; break; }
        case ToolType::Redact: { auto *r = new RedactItem(RedactMode::Blur, m_bg, QRectF(p,p)); m_active=r; break; }
        case ToolType::Spotlight: { auto *s = new SpotlightItem(QRectF(p,p), m_bg.size()); m_active=s; break; }
        case ToolType::Pen: { auto *pp = new PenPathItem(p); style(pp,m_color,m_width); m_active=pp; break; }
        default: break; // Move/Text handled elsewhere
    }
    if (m_active) {
        m_active->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        m_scene->addItem(m_active); // live preview; committed in finish()
    }
}

void ToolController::update(const QPointF &p, Qt::KeyboardModifiers modifiers) {
    if (!m_active) return;
    if (auto *a = dynamic_cast<ArrowItem*>(m_active))
        a->setEnd(constrainedEnd(m_start, p, modifiers, true));
    else if (auto *r = dynamic_cast<RectItem*>(m_active)) r->setRect(creationRect(m_start,p,modifiers));
    else if (auto *e = dynamic_cast<EllipseItem*>(m_active)) e->setRect(creationRect(m_start,p,modifiers));
    else if (auto *h = dynamic_cast<HighlightItem*>(m_active)) h->setRect(creationRect(m_start,p,modifiers));
    else if (auto *rd = dynamic_cast<RedactItem*>(m_active)) rd->setRect(creationRect(m_start,p,modifiers));
    else if (auto *s = dynamic_cast<SpotlightItem*>(m_active)) s->setRect(creationRect(m_start,p,modifiers));
    else if (auto *pp = dynamic_cast<PenPathItem*>(m_active)) pp->addPoint(p);
}

void ToolController::finish(const QPointF &p, Qt::KeyboardModifiers modifiers) {
    if (!m_active) return;
    update(p, modifiers);
    finalizeFade();                  // settle prior fade before push() can discard the redo branch
    QGraphicsItem *committed = m_active;
    m_scene->removeItem(m_active);
    if (dynamic_cast<SpotlightItem *>(m_active)) {
        SpotlightItem *old = nullptr;
        for (QGraphicsItem *item : m_scene->items())
            if ((old = dynamic_cast<SpotlightItem *>(item))) break;
        m_undo->beginMacro(QStringLiteral("Replace Spotlight"));
        if (old) m_undo->push(new RemoveItemCommand(m_scene, old));
        m_undo->push(new AddItemCommand(m_scene, m_active));
        m_undo->endMacro();
    } else {
        m_undo->push(new AddItemCommand(m_scene, m_active));
    }
    m_active = nullptr;
    fadeIn(committed);
    if (dynamic_cast<RedactItem *>(committed) || dynamic_cast<SpotlightItem *>(committed)) {
        m_scene->clearSelection();
        committed->setSelected(true);   // show the context bar immediately
        setTool(ToolType::Move);        // ready to adjust the retained region
    }
}

bool ToolController::cancelActive() {
    if (!m_active) return false;
    m_scene->removeItem(m_active);
    delete m_active;
    m_active = nullptr;
    return true;
}

QGraphicsItem *ToolController::placeText(const QPointF &p) {
    commitTextEdit();
    auto *t = new TextItem(QString(), m_color, qMax(14.0, m_width * 4.0));
    if (!m_textFont.isEmpty()) {
        QFont font = t->font();
        font.setFamily(m_textFont);
        t->setFont(font);
    }
    t->setPos(p);
    t->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    t->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_scene->addItem(t);
    m_scene->clearSelection();
    t->setSelected(true);
    m_editingText = t;
    m_textBefore = t->state();
    m_textIsNew = true;
    t->setFocus();
    return t;
}

bool ToolController::editText(TextItem *item) {
    if (!item || !item->scene()) return false;
    if (m_editingText == item) return true;
    commitTextEdit();
    m_editingText = item;
    m_textBefore = item->state();
    m_textIsNew = false;
    m_scene->clearSelection();
    item->setSelected(true);
    item->setTextInteractionFlags(Qt::TextEditorInteraction);
    item->setFocus();
    return true;
}

bool ToolController::commitTextEdit() {
    if (!m_editingText) return false;
    TextItem *item = m_editingText;
    const TextState before = m_textBefore;
    const bool isNew = m_textIsNew;
    m_editingText = nullptr;
    m_textIsNew = false;
    item->setTextInteractionFlags(Qt::NoTextInteraction);
    item->clearFocus();
    if (isNew) {
        if (item->toPlainText().trimmed().isEmpty()) {
            m_scene->removeItem(item);
            delete item;
        } else {
            finalizeFade();
            m_scene->removeItem(item);
            m_undo->push(new AddItemCommand(m_scene, item));
            item->setSelected(true);
            fadeIn(item);
        }
    } else {
        const TextState after = item->state();
        if (!(after == before)) m_undo->push(new EditTextCommand(item, before, after));
    }
    return true;
}

bool ToolController::cancelTextEdit() {
    if (!m_editingText) return false;
    TextItem *item = m_editingText;
    const TextState before = m_textBefore;
    const bool isNew = m_textIsNew;
    m_editingText = nullptr;
    m_textIsNew = false;
    item->setTextInteractionFlags(Qt::NoTextInteraction);
    item->clearFocus();
    if (isNew) {
        m_scene->removeItem(item);
        delete item;
    } else {
        item->applyState(before);
    }
    return true;
}

void ToolController::beginMove() {
    m_moveItems.clear();
    m_moveBefore.clear();
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (item->flags().testFlag(QGraphicsItem::ItemIsMovable)) {
            m_moveItems.append(item);
            m_moveBefore.append(item->pos());
        }
    }
}

void ToolController::finishMove() {
    QList<QPointF> after;
    bool changed = false;
    for (qsizetype i = 0; i < m_moveItems.size(); ++i) {
        after.append(m_moveItems[i]->pos());
        changed |= after.last() != m_moveBefore[i];
    }
    if (changed)
        m_undo->push(new MoveItemsCommand(m_moveItems, m_moveBefore, after));
    m_moveItems.clear();
    m_moveBefore.clear();
    if (m_duplicateMove) {
        m_duplicateMove = false;
        m_undo->endMacro();
    }
}

static QGraphicsItem *cloneItem(const QGraphicsItem *item) {
    if (dynamic_cast<const SpotlightItem *>(item)) return nullptr;
    if (auto *annotation = dynamic_cast<const AnnotationItem *>(item)) return annotation->clone();
    if (auto *text = dynamic_cast<const TextItem *>(item)) return text->clone();
    return nullptr;
}

static void copyItemState(const QGraphicsItem *source, QGraphicsItem *copy, const QPointF &offset) {
    copy->setFlags(source->flags());
    copy->setTransform(source->transform());
    copy->setTransformOriginPoint(source->transformOriginPoint());
    copy->setRotation(source->rotation());
    copy->setScale(source->scale());
    copy->setOpacity(source->opacity());
    copy->setZValue(source->zValue());
    copy->setPos(source->pos() + offset);
}

bool ToolController::duplicateSelection(const QPointF &offset) {
    QList<QGraphicsItem *> copies;
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (QGraphicsItem *copy = cloneItem(item)) {
            copyItemState(item, copy, offset);
            copies.append(copy);
        }
    }
    if (copies.isEmpty()) return false;

    m_scene->clearSelection();
    m_undo->beginMacro(QStringLiteral("Duplicate"));
    for (QGraphicsItem *copy : copies) {
        m_undo->push(new AddItemCommand(m_scene, copy));
        copy->setSelected(true);
    }
    m_undo->endMacro();
    return true;
}

bool ToolController::beginDuplicateMove() {
    if (m_duplicateMove) return false;
    m_undo->beginMacro(QStringLiteral("Duplicate and move"));
    if (!duplicateSelection()) {
        m_undo->endMacro();
        return false;
    }
    m_duplicateMove = true;
    beginMove();
    return true;
}

bool ToolController::nudgeSelection(const QPointF &delta) {
    if (delta.isNull()) return false;
    QList<QGraphicsItem *> items;
    QList<QPointF> before;
    QList<QPointF> after;
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (!item->flags().testFlag(QGraphicsItem::ItemIsMovable)) continue;
        items.append(item);
        before.append(item->pos());
        after.append(item->pos() + delta);
    }
    if (items.isEmpty()) return false;
    m_undo->push(new MoveItemsCommand(items, before, after));
    return true;
}

}
