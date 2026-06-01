#pragma once
#include <QUndoCommand>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QRectF>
#include <QPointF>
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

class AnnotationItem; class ArrowItem; class RedactItem;
enum class RedactMode;

// Resize for the four rect-shaped items (Rect/Ellipse/Highlight/Redact) through
// AnnotationItem's virtual rect()/setRect().
class ResizeRectCommand : public QUndoCommand {
public:
    ResizeRectCommand(class AnnotationItem *it, const QRectF &before, const QRectF &after);
    void undo() override; void redo() override;
private:
    class AnnotationItem *m_it; QRectF m_before, m_after;
};
class ResizeArrowCommand : public QUndoCommand {
public:
    ResizeArrowCommand(class ArrowItem *it, QPointF s0, QPointF e0, QPointF s1, QPointF e1);
    void undo() override; void redo() override;
private:
    class ArrowItem *m_it; QPointF m_s0,m_e0,m_s1,m_e1;
};
// Undoable redact mode switch (Blur/Blacken/OcrBlur/OcrBlacken).
class SetRedactModeCommand : public QUndoCommand {
public:
    SetRedactModeCommand(class RedactItem *it, RedactMode before, RedactMode after);
    void undo() override; void redo() override;
private:
    class RedactItem *m_it; RedactMode m_before, m_after;
};
}
