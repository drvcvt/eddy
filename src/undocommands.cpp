#include "undocommands.h"
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
}
