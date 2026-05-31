#pragma once
#include <QObject>
#include <QVector>
class QGraphicsScene; class QGraphicsItem; class QUndoStack;
namespace eddy {
class HandleItem;
// Watches a scene's selection and draws drag handles on the single selected
// resizable annotation (rect-shaped → 8 handles; ArrowItem → 2 endpoint handles).
// Resizes live and pushes one undo command on release. Handles are overlay items
// (high zValue) and are never part of the saved image.
class SelectionHandles : public QObject {
    Q_OBJECT
public:
    explicit SelectionHandles(QGraphicsScene *scene, QUndoStack *undo = nullptr, QObject *parent = nullptr);
    int handleCount() const;
    void refresh();        // rebuild handles for the current selection (on selectionChanged)
    void reposition();     // move existing handles to the target's current geometry (during drag)
private:
    QGraphicsScene *m_scene;
    QUndoStack *m_undo;
    QVector<HandleItem*> m_handles;
    QGraphicsItem *m_target = nullptr;
    void clear();
};
}
