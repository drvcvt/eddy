#pragma once
#include <QUndoCommand>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QRectF>
#include <QPointF>
#include <QList>
#include <functional>
#include "items/textitem.h"
#include "items/spotlightitem.h"
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
class MoveItemsCommand : public QUndoCommand {
public:
    MoveItemsCommand(const QList<QGraphicsItem *> &items,
                     const QList<QPointF> &before, const QList<QPointF> &after);
    void undo() override; void redo() override;
private:
    QList<QGraphicsItem *> m_items;
    QList<QPointF> m_before, m_after;
};

class AnnotationItem; class ArrowItem; class RedactItem;
enum class RedactMode;

// Resize for the rect-shaped items (Rect/Ellipse/Highlight/Redact/Spotlight) through
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
class EditTextCommand : public QUndoCommand {
public:
    EditTextCommand(TextItem *item, const TextState &before, const TextState &after);
    void undo() override; void redo() override;
private:
    TextItem *m_item;
    TextState m_before;
    TextState m_after;
};
class SetSpotlightStyleCommand : public QUndoCommand {
public:
    SetSpotlightStyleCommand(SpotlightItem *item, SpotlightShape beforeShape,
                             int beforeIntensity, SpotlightShape afterShape,
                             int afterIntensity);
    void undo() override; void redo() override;
private:
    SpotlightItem *m_item;
    SpotlightShape m_beforeShape, m_afterShape;
    int m_beforeIntensity, m_afterIntensity;
};
class SetTrimRangeCommand : public QUndoCommand {
public:
    using Apply = std::function<void(qint64, qint64)>;
    SetTrimRangeCommand(qint64 beforeIn, qint64 beforeOut,
                        qint64 afterIn, qint64 afterOut, Apply apply);
    void undo() override; void redo() override;
private:
    qint64 m_beforeIn, m_beforeOut, m_afterIn, m_afterOut;
    Apply m_apply;
};
}
