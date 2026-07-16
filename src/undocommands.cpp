#include "undocommands.h"
#include "items/annotationitem.h"
#include "items/arrowitem.h"
#include "items/redactitem.h"
#include "items/textitem.h"
namespace eddy {
AddItemCommand::AddItemCommand(QGraphicsScene *scene, QGraphicsItem *item)
    : m_scene(scene), m_item(item) { setText("add annotation"); }
AddItemCommand::~AddItemCommand() { if (!m_inScene) delete m_item; }
void AddItemCommand::redo() { m_scene->addItem(m_item); m_inScene = true; }
void AddItemCommand::undo() { m_scene->removeItem(m_item); m_inScene = false; }

RemoveItemCommand::RemoveItemCommand(QGraphicsScene *scene, QGraphicsItem *item)
    : m_scene(scene), m_item(item) { setText("delete annotation"); }
RemoveItemCommand::~RemoveItemCommand() { if (!m_inScene) delete m_item; }
void RemoveItemCommand::redo() { m_scene->removeItem(m_item); m_inScene = false; }
void RemoveItemCommand::undo() { m_scene->addItem(m_item); m_inScene = true; }

MoveItemsCommand::MoveItemsCommand(const QList<QGraphicsItem *> &items,
                                   const QList<QPointF> &before, const QList<QPointF> &after)
    : m_items(items), m_before(before), m_after(after) { setText("move"); }
void MoveItemsCommand::redo() {
    for (qsizetype i = 0; i < m_items.size(); ++i) m_items[i]->setPos(m_after[i]);
}
void MoveItemsCommand::undo() {
    for (qsizetype i = 0; i < m_items.size(); ++i) m_items[i]->setPos(m_before[i]);
}

ResizeRectCommand::ResizeRectCommand(AnnotationItem *it, const QRectF &b, const QRectF &a)
    : m_it(it), m_before(b), m_after(a) { setText("resize"); }
void ResizeRectCommand::redo() { m_it->setRect(m_after); }   // virtual → concrete setRect
void ResizeRectCommand::undo() { m_it->setRect(m_before); }

ResizeArrowCommand::ResizeArrowCommand(ArrowItem *it, QPointF s0, QPointF e0, QPointF s1, QPointF e1)
    : m_it(it), m_s0(s0), m_e0(e0), m_s1(s1), m_e1(e1) { setText("resize"); }
void ResizeArrowCommand::redo() { m_it->setStart(m_s1); m_it->setEnd(m_e1); }
void ResizeArrowCommand::undo() { m_it->setStart(m_s0); m_it->setEnd(m_e0); }

SetRedactModeCommand::SetRedactModeCommand(RedactItem *it, RedactMode before, RedactMode after)
    : m_it(it), m_before(before), m_after(after) { setText("redact mode"); }
void SetRedactModeCommand::redo() { m_it->setMode(m_after); }
void SetRedactModeCommand::undo() { m_it->setMode(m_before); }

EditTextCommand::EditTextCommand(TextItem *item, const TextState &before, const TextState &after)
    : m_item(item), m_before(before), m_after(after) {
    setText("edit text");
}
void EditTextCommand::undo() { m_item->applyState(m_before); }
void EditTextCommand::redo() { m_item->applyState(m_after); }

SetSpotlightStyleCommand::SetSpotlightStyleCommand(
    SpotlightItem *item, SpotlightShape beforeShape, int beforeIntensity,
    SpotlightShape afterShape, int afterIntensity)
    : m_item(item), m_beforeShape(beforeShape), m_afterShape(afterShape),
      m_beforeIntensity(beforeIntensity), m_afterIntensity(afterIntensity) {
    setText("spotlight style");
}
void SetSpotlightStyleCommand::undo() {
    m_item->setSpotlightShape(m_beforeShape); m_item->setIntensity(m_beforeIntensity);
}
void SetSpotlightStyleCommand::redo() {
    m_item->setSpotlightShape(m_afterShape); m_item->setIntensity(m_afterIntensity);
}

SetTrimRangeCommand::SetTrimRangeCommand(
    qint64 beforeIn, qint64 beforeOut, qint64 afterIn, qint64 afterOut, Apply apply)
    : m_beforeIn(beforeIn), m_beforeOut(beforeOut), m_afterIn(afterIn),
      m_afterOut(afterOut), m_apply(std::move(apply)) {
    setText("trim video");
}
void SetTrimRangeCommand::undo() { m_apply(m_beforeIn, m_beforeOut); }
void SetTrimRangeCommand::redo() { m_apply(m_afterIn, m_afterOut); }
}
