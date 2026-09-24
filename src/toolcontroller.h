#pragma once
#include <QObject>
#include <QColor>
#include <QPointF>
#include <QImage>
#include <QPointer>
#include <QString>
#include <QList>
#include "items/textitem.h"
class QGraphicsScene; class QUndoStack; class QGraphicsItem; class QVariantAnimation;
namespace eddy {

enum class ToolType { Move, Arrow, Pen, Rect, Ellipse, Highlight, Text, Redact, Spotlight, Crop };

ToolType toolFromName(const QString &name);

class ToolController : public QObject {
    Q_OBJECT
public:
    ToolController(QGraphicsScene *scene, QUndoStack *undo, QImage background, QObject *parent=nullptr);
    void setTool(ToolType t);
    ToolType tool() const { return m_tool; }
    void setColor(const QColor &c) { m_color = c; }
    void setWidth(double w) { m_width = w; }
    void setTextFont(const QString &family) { m_textFont = family; }
    void setAnimationsEnabled(bool on) { m_animations = on; }
    void setBackground(const QImage &background) { m_bg = background; }
    // Videos: where the playhead is, so a new spotlight only replaces the one
    // showing there (decision E6).
    void setVideoTime(qint64 playheadMs, qint64 durationMs) { m_playhead = playheadMs; m_duration = durationMs; }

    void begin(const QPointF &p);
    void update(const QPointF &p, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void finish(const QPointF &p, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    bool cancelActive();
    QGraphicsItem *placeText(const QPointF &p);
    bool editText(TextItem *item);
    bool commitTextEdit();
    bool cancelTextEdit();
    TextItem *editingText() const { return m_editingText; }
    void beginMove();
    void finishMove();
    bool duplicateSelection(const QPointF &offset = QPointF());
    bool nudgeSelection(const QPointF &delta);
    bool beginDuplicateMove();

signals:
    void toolChanged(ToolType t);

private:
    void fadeIn(QGraphicsItem *it);   // 0->1 opacity on commit (no-op if disabled)
    void finalizeFade();

    QGraphicsScene *m_scene; QUndoStack *m_undo; QImage m_bg;
    ToolType m_tool = ToolType::Arrow;
    QColor m_color = QColor("#ff3b30");
    QString m_textFont;
    double m_width = 4.0;
    bool m_animations = true;
    QGraphicsItem *m_active = nullptr;
    QPointer<QVariantAnimation> m_fadeAnim;   // the one in-flight commit fade
    QGraphicsItem *m_fadingItem = nullptr;    // item that fade belongs to (raw: not a QObject)
    QPointF m_start;
    QList<QGraphicsItem *> m_moveItems;
    QList<QPointF> m_moveBefore;
    bool m_duplicateMove = false;
    qint64 m_playhead = -1, m_duration = 0;   // -1: not a video
    QPointer<TextItem> m_editingText;
    TextState m_textBefore;
    bool m_textIsNew = false;
};
}
