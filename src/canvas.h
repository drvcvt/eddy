#pragma once
#include <QGraphicsView>
#include "toolcontroller.h"
namespace eddy {
class Canvas : public QGraphicsView {
    Q_OBJECT
public:
    Canvas(QGraphicsScene *scene, ToolController *tools, QWidget *parent=nullptr);
    double zoom() const { return m_zoom; }
protected:
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
private:
    // Tools that use native view interaction instead of drag-to-draw.
    bool isPointerTool() const {
        return m_tools->tool() == ToolType::Move || m_tools->tool() == ToolType::Text;
    }
    ToolController *m_tools; double m_zoom = 1.0; bool m_dragging = false;
};
}
