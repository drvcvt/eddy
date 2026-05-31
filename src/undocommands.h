#pragma once
#include <QUndoCommand>
#include <QGraphicsScene>
#include <QGraphicsItem>
namespace eddy {
class AddItemCommand : public QUndoCommand {
public:
    AddItemCommand(QGraphicsScene *scene, QGraphicsItem *item);
    ~AddItemCommand() override;
    void undo() override; void redo() override;
private:
    QGraphicsScene *m_scene; QGraphicsItem *m_item; bool m_inScene = false;
};
class RemoveItemCommand : public QUndoCommand {
public:
    RemoveItemCommand(QGraphicsScene *scene, QGraphicsItem *item);
    ~RemoveItemCommand() override;
    void undo() override; void redo() override;
private:
    QGraphicsScene *m_scene; QGraphicsItem *m_item; bool m_inScene = true;
};
}
