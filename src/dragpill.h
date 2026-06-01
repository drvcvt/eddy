#pragma once
#include <QWidget>
#include <QString>
#include <functional>

class QImage;
class QMimeData;

namespace eddy {

// Builds drop data carrying `img` BOTH as image data (image/png) and as a temp
// PNG file URL (text/uri-list). Writes the temp file; on success sets *tempPathOut
// to its path. The returned QMimeData is owned by the caller (QDrag takes it over).
QMimeData *makeImageDropMime(const QImage &img, QString *tempPathOut = nullptr);

// A bottom-of-canvas handle: press and drag it into another window to drop the
// current composite. The image is fetched from an injected provider at drag time.
class DragPill : public QWidget {
    Q_OBJECT
public:
    explicit DragPill(QWidget *parent = nullptr);
    ~DragPill() override;
    void setImageProvider(std::function<QImage()> provider);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    void startDrag();
    std::function<QImage()> m_provider;
    QPoint m_pressPos;
    QString m_lastTempPath;   // previous drag's temp file, removed before the next / on destroy
};

}
