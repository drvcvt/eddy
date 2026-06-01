#pragma once
#include <QGraphicsView>
#include "toolcontroller.h"
class QVariantAnimation;
class QResizeEvent;
namespace eddy {
class Canvas : public QGraphicsView {
    Q_OBJECT
public:
    Canvas(QGraphicsScene *scene, ToolController *tools, QWidget *parent=nullptr);
    double zoom() const { return m_targetZoom; }       // logical target (test-stable)
    void setAnimationsEnabled(bool on) { m_animations = on; }
signals:
    void viewChanged();   // emitted on zoom / pan / resize so overlays can re-anchor
protected:
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
private:
    bool isPointerTool() const {
        return m_tools->tool() == ToolType::Move || m_tools->tool() == ToolType::Text;
    }
    ToolController *m_tools;
    double m_zoom = 1.0;            // visual (animated) scale
    double m_targetZoom = 1.0;      // logical target
    bool m_dragging = false;
    bool m_animations = true;
    QVariantAnimation *m_zoomAnim = nullptr;
};
}
