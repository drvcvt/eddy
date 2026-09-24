#pragma once
#include <QGraphicsView>
#include <QPoint>
#include <QImage>
#include "toolcontroller.h"
class QVariantAnimation;
class QResizeEvent;
class QKeyEvent;
namespace eddy {
class Loupe;
class CropController;
class Canvas : public QGraphicsView {
    Q_OBJECT
public:
    Canvas(QGraphicsScene *scene, ToolController *tools, QWidget *parent=nullptr);
    double zoom() const { return m_targetZoom; }       // logical target (test-stable)
    void setAnimationsEnabled(bool on) { m_animations = on; }
    void zoomBy(double factor);
    void resetZoom();
    void fitMedia();
    void setContentRect(QRectF rect);
    QRectF contentRect() const;
    void setCropController(CropController *crop);
    // Studio preview: `background` fills `output` (scene coordinates) around
    // the content rect, whose corners are rounded by `radius` scene units.
    void setStudioFrame(const QPixmap &background, QRectF output, qreal radius);
    void clearStudioFrame();
    // Studio zoom preview (studio plan 5): the `camera` window (document
    // pixels) fills the place of the content rect. The Studio frame and the
    // user's own zoom and pan stay where they are.
    void setCamera(const QRectF &camera);
    QRectF camera() const { return m_cameraRect; }
    // Move-tool drags on empty content steer the camera instead of selecting.
    void setCameraDragEnabled(bool on) { m_cameraDrag = on; }
    bool cameraDragging() const { return m_cameraDragging; }
    void cancelCameraDrag();
    bool fitted() const { return m_fitted; }
    void restoreView(const QTransform &transform, QPointF center, bool fitted);
    void setSpacePan(bool on);
    void cancelPan();
    bool spacePanActive() const { return m_spacePan; }
    // Eyedropper: freeze the rendered viewport and let the user pick a colour off
    // it with a magnifier loupe. Emits colorPicked() on click, nothing on cancel.
    void startEyedropper();
    void cancelEyedropper();
    bool eyedropperActive() const { return m_eyedropper; }
signals:
    void viewChanged();   // emitted on zoom / pan / resize so overlays can re-anchor
    void colorPicked(const QColor &c);
    void cameraDragged(QPointF documentDelta);   // the pointer moved this far over the document
    void cameraDragFinished(bool cancelled);
protected:
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
private:
    bool isPointerTool() const {
        return m_tools->tool() == ToolType::Move || m_tools->tool() == ToolType::Text;
    }
    QPoint sourcePixel(const QPoint &viewPos) const;   // viewport px -> snapshot device px
    void updateLoupe(const QPoint &viewPos);
    void updateCursor();
    void updateNavigationBounds();
    template <typename Change> void withoutCamera(Change change);
    void applyCamera();
    QTransform viewWithoutCamera() const;
    QPointF viewportCentreInScene() const;
    QTransform m_camera;         // scene -> scene: the camera window onto the content rect
    QRectF m_cameraRect;
    double m_viewZoom = 1.0;     // the user's own view while a camera is on
    QPointF m_viewCentre;
    bool m_cameraDrag = false, m_cameraDragging = false;
    QPointF m_cameraDragLast;
    ToolController *m_tools;
    CropController *m_crop = nullptr;
    QRectF m_contentRect;
    QRectF viewRect() const;        // what Fit shows: the Studio frame or the content
    QPixmap m_studioBackground;
    QRectF m_studioOutput;
    qreal m_studioRadius = 0;
    bool m_fitted = false;
    double m_zoom = 1.0;            // visual (animated) scale
    double m_targetZoom = 1.0;      // logical target
    bool m_dragging = false;        // middle-button pan in progress
    Qt::MouseButton m_panButton = Qt::NoButton;
    QPoint m_panLast;               // last cursor pos during a pan (viewport coords)
    bool m_spacePan = false;
    bool m_duplicateDragging = false;
    Qt::MouseButton m_swallowRelease = Qt::NoButton;
    bool m_animations = true;
    QVariantAnimation *m_zoomAnim = nullptr;
    bool m_eyedropper = false;      // eyedropper colour-pick in progress
    QImage m_eyeShot;               // frozen viewport snapshot (device px) being sampled
    qreal m_eyeDpr = 1.0;           // snapshot device-pixel ratio
    bool m_eyeTrackPrev = false;    // viewport mouse-tracking state to restore on cancel
    Loupe *m_loupe = nullptr;
};
}
