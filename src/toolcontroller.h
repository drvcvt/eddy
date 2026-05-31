#pragma once
#include <QObject>
#include <QColor>
#include <QPointF>
#include <QImage>
#include <QPointer>
class QGraphicsScene; class QUndoStack; class QGraphicsItem; class QVariantAnimation;
namespace eddy {

enum class ToolType { Move, Arrow, Pen, Rect, Ellipse, Highlight, Text, Blur, Pixelate, Redact };

ToolType toolFromName(const QString &name);

class ToolController : public QObject {
    Q_OBJECT
public:
    ToolController(QGraphicsScene *scene, QUndoStack *undo, QImage background, QObject *parent=nullptr);
    void setTool(ToolType t);
    ToolType tool() const { return m_tool; }
    void setColor(const QColor &c) { m_color = c; }
    void setWidth(double w) { m_width = w; }
    void setAnimationsEnabled(bool on) { m_animations = on; }

    void begin(const QPointF &p);
    void update(const QPointF &p);
    void finish(const QPointF &p);
    QGraphicsItem *placeText(const QPointF &p);

signals:
    void toolChanged(ToolType t);

private:
    void fadeIn(QGraphicsItem *it);   // 0->1 opacity on commit (no-op if disabled)
    void finalizeFade();

    QGraphicsScene *m_scene; QUndoStack *m_undo; QImage m_bg;
    ToolType m_tool = ToolType::Arrow;
    QColor m_color = QColor("#ff3b30");
    double m_width = 4.0;
    bool m_animations = true;
    QGraphicsItem *m_active = nullptr;
    QPointer<QVariantAnimation> m_fadeAnim;   // the one in-flight commit fade
    QGraphicsItem *m_fadingItem = nullptr;    // item that fade belongs to (raw: not a QObject)
    QPointF m_start;
};
}
