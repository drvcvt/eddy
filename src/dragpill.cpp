#include "dragpill.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QImage>
#include <QPixmap>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>
#include <QUrl>

namespace eddy {

QMimeData *makeImageDropMime(const QImage &img, QString *tempPathOut) {
    auto *mime = new QMimeData;
    mime->setImageData(img);                                   // image/png for editors/chat/browsers
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/eddy-XXXXXX.png"));
    tmp.setAutoRemove(false);
    if (tmp.open()) {
        const QString path = tmp.fileName();
        tmp.close();
        if (img.save(path, "PNG")) {
            mime->setUrls({ QUrl::fromLocalFile(path) });      // text/uri-list for file-drop targets
            if (tempPathOut) *tempPathOut = path;
        } else {
            QFile::remove(path);
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
    drag->exec(Qt::CopyAction);
    setCursor(Qt::OpenHandCursor);
}

}
