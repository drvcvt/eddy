#pragma once
#include <QGraphicsView>
#include <QPoint>
#include <QImage>
#include "toolcontroller.h"
class QVariantAnimation;
class QResizeEvent;
namespace eddy {
class Loupe;
class Canvas : public QGraphicsView {
    Q_OBJECT
public:
    Canvas(QGraphicsScene *scene, ToolController *tools, QWidget *parent=nullptr);
    double zoom() const { return m_targetZoom; }       // logical target (test-stable)
    void setAnimationsEnabled(bool on) { m_animations = on; }
    // Eyedropper: freeze the rendered viewport and let the user pick a colour off
    // it with a magnifier loupe. Emits colorPicked() on click, nothing on cancel.
    void startEyedropper();
    void cancelEyedropper();
    bool eyedropperActive() const { return m_eyedropper; }
signals:
    void viewChanged();   // emitted on zoom / pan / resize so overlays can re-anchor
    void colorPicked(const QColor &c);
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
    QPoint sourcePixel(const QPoint &viewPos) const;   // viewport px -> snapshot device px
    void updateLoupe(const QPoint &viewPos);
    ToolController *m_tools;
    double m_zoom = 1.0;            // visual (animated) scale
    double m_targetZoom = 1.0;      // logical target
    bool m_dragging = false;        // middle-button pan in progress
    QPoint m_panLast;               // last cursor pos during a pan (viewport coords)
    bool m_animations = true;
    QVariantAnimation *m_zoomAnim = nullptr;
    bool m_eyedropper = false;      // eyedropper colour-pick in progress
    QImage m_eyeShot;               // frozen viewport snapshot (device px) being sampled
    qreal m_eyeDpr = 1.0;           // snapshot device-pixel ratio
    bool m_eyeTrackPrev = false;    // viewport mouse-tracking state to restore on cancel
    Loupe *m_loupe = nullptr;
};
}
