#include "undocommands.h"
#include "items/annotationitem.h"
#include "items/arrowitem.h"
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

ResizeRectCommand::ResizeRectCommand(AnnotationItem *it, const QRectF &b, const QRectF &a)
    : m_it(it), m_before(b), m_after(a) { setText("resize"); }
void ResizeRectCommand::redo() { m_it->setRect(m_after); }   // virtual → concrete setRect
void ResizeRectCommand::undo() { m_it->setRect(m_before); }

ResizeArrowCommand::ResizeArrowCommand(ArrowItem *it, QPointF s0, QPointF e0, QPointF s1, QPointF e1)
    : m_it(it), m_s0(s0), m_e0(e0), m_s1(s1), m_e1(e1) { setText("resize"); }
void ResizeArrowCommand::redo() { m_it->setStart(m_s1); m_it->setEnd(m_e1); }
void ResizeArrowCommand::undo() { m_it->setStart(m_s0); m_it->setEnd(m_e0); }
}
