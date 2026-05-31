#pragma once
#include <QObject>
#include <QColor>
#include <QPointF>
#include <QImage>
class QGraphicsScene; class QUndoStack; class QGraphicsItem;
namespace eddy {

enum class ToolType { Move, Arrow, Pen, Rect, Ellipse, Highlight, Text, Blur, Pixelate, Redact };

ToolType toolFromName(const QString &name);

class ToolController : public QObject {
    Q_OBJECT
public:
    ToolController(QGraphicsScene *scene, QUndoStack *undo, QImage background, QObject *parent=nullptr);
    void setTool(ToolType t) { m_tool = t; }
    ToolType tool() const { return m_tool; }
    void setColor(const QColor &c) { m_color = c; }
    void setWidth(double w) { m_width = w; }

    // Drag lifecycle in scene coordinates.
    void begin(const QPointF &p);
    void update(const QPointF &p);
    void finish(const QPointF &p);

    // Text tool: create an editable text item at p (committed to the undo
    // stack) and return it so the view can give it keyboard focus.
    QGraphicsItem *placeText(const QPointF &p);

private:
    QGraphicsScene *m_scene; QUndoStack *m_undo; QImage m_bg;
    ToolType m_tool = ToolType::Arrow;
    QColor m_color = QColor("#ff3b30");
    double m_width = 4.0;
    QGraphicsItem *m_active = nullptr;  // item being dragged out
    QPointF m_start;
};
}
