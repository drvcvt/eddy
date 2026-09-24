#pragma once
#include <QImage>
#include <QWidget>
namespace eddy {

// Where the camera looks while a zoom is edited (Q3 = C): the whole content,
// the camera's window bright and the rest dimmed. Dragging moves the window,
// the wheel zooms. Editor chrome only, never exported.
class MiniMap : public QWidget {
    Q_OBJECT
public:
    explicit MiniMap(QWidget *parent = nullptr);
    // `content` is the document rect `image` shows.
    void setContent(const QImage &image, const QRectF &content);
    void setCamera(const QRectF &camera);
    // The picture's largest width; the map shrinks with small content.
    void setWidthLimit(int width);
signals:
    void dragged(QPointF documentDelta);
    void dragFinished(bool cancelled);
    void wheelZoom(double steps);   // +1 per notch zooms in
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    QRectF imageRect() const { return QRectF(rect()).adjusted(4, 4, -4, -4); }
    QRectF windowRect() const;
    void fitSize();
    QImage m_image;
    QRectF m_content, m_camera;
    int m_widthLimit = 152;
    bool m_dragging = false;
    QPointF m_last;
};

}
