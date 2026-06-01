#include "dragpill.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QDrag>
#include <QMimeData>
#include <QImage>
#include <QPixmap>
#include <QBuffer>
#include <QByteArray>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QUrl>

namespace eddy {

// Mirrors boltsnap's drag payload: explicit image/png bytes (for chat/browser/editor
// targets that look for that exact type) + a text/uri-list pointing to a real PNG file
// (for file-drop targets). setImageData adds application/x-qt-image for Qt-native apps.
QMimeData *makeImageDropMime(const QImage &img, QString *tempPathOut) {
    QByteArray png;
    {
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
    }
    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("image/png"), png);           // explicit PNG bytes
    mime->setImageData(img);                                   // application/x-qt-image
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/eddy-XXXXXX.png"));
    tmp.setAutoRemove(false);
    if (tmp.open()) {
        if (tmp.write(png) == png.size()) {
            tmp.close();
            const QString abs = QFileInfo(tmp.fileName()).canonicalFilePath();
            mime->setUrls({ QUrl::fromLocalFile(abs) });       // text/uri-list (absolute path)
            if (tempPathOut) *tempPathOut = abs;
        } else {
            const QString p = tmp.fileName();
            tmp.close();
            QFile::remove(p);
        }
    }
    return mime;
}

DragPill::DragPill(QWidget *parent) : QWidget(parent) {
    setObjectName("DragPill");
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::OpenHandCursor);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 6, 12, 6);
    auto *label = new QLabel(QString::fromUtf8("\xE2\xA4\x93  drag out"), this);   // "⤓  drag out"
    label->setObjectName("DragPillText");
    lay->addWidget(label);
}

DragPill::~DragPill() {
    if (!m_lastTempPath.isEmpty()) QFile::remove(m_lastTempPath);
}

void DragPill::setImageProvider(std::function<QImage()> provider) { m_provider = std::move(provider); }

void DragPill::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) { m_pressPos = e->pos(); e->accept(); }
    else QWidget::mousePressEvent(e);
}

void DragPill::mouseMoveEvent(QMouseEvent *e) {
    if ((e->buttons() & Qt::LeftButton)
        && (e->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        startDrag();
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void DragPill::startDrag() {
    if (!m_provider) return;
    const QImage img = m_provider();
    if (img.isNull()) return;

    QString path;
    QMimeData *mime = makeImageDropMime(img, &path);
    if (!m_lastTempPath.isEmpty()) QFile::remove(m_lastTempPath);   // keep at most one temp file
    m_lastTempPath = path;

    setCursor(Qt::ClosedHandCursor);
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);                                       // QDrag takes ownership
    drag->setPixmap(QPixmap::fromImage(
        img.scaled(180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    const Qt::DropAction result = drag->exec(Qt::CopyAction);
    if (result == Qt::IgnoreAction)                            // dropped on nothing: don't waste it
        QGuiApplication::clipboard()->setImage(img);           // (boltsnap's cancel fallback)
    setCursor(Qt::OpenHandCursor);
}

}
