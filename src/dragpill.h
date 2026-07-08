#pragma once
#include <QWidget>
#include <QString>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <functional>

class QMimeData;

namespace eddy {

struct FileDragPayload {
    QString path;
    QString mimeType;
    QImage preview;
    bool removeAfterUse = true;
    bool includeBytesInQtMime = true;
};

// Builds a file-drop payload that exposes only a text/uri-list URL. Use this
// for large media (videos) so Qt never pulls the whole file into QByteArray
// memory on the GUI thread during DnD.
QMimeData *makeUrlDropMime(const QString &path);

// Builds drop data backed by `path`, offering `mimeType` bytes lazily plus a
// text/uri-list file URL. This mirrors boltsnap's robust DnD contract: targets
// can either request the concrete MIME bytes or consume the file URI.
QMimeData *makeFileDropMime(const QString &path, const QString &mimeType);

// Builds drop data carrying `img` BOTH as image data (image/png) and as a temp
// PNG file URL (text/uri-list). Writes the temp file; on success sets *tempPathOut
// to its path. The returned QMimeData is owned by the caller (QDrag takes it over).
QMimeData *makeImageDropMime(const QImage &img, QString *tempPathOut = nullptr);

QPixmap makeDragGhostPixmap(const QImage &preview, const QSize &maxSize = QSize(180, 180));

// A bottom-of-canvas handle: press and drag it into another window to drop the
// current composite. The image is fetched from an injected provider at drag time.
class DragPill : public QWidget {
    Q_OBJECT
public:
    explicit DragPill(QWidget *parent = nullptr);
    ~DragPill() override;
    void setImageProvider(std::function<QImage()> provider);
    void setFileProvider(std::function<FileDragPayload()> provider);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    void startDrag();
    std::function<QImage()> m_provider;
    std::function<FileDragPayload()> m_fileProvider;
    QPoint m_pressPos;
    QString m_lastTempPath;   // previous generated temp file, removed before the next / on destroy
};

}
