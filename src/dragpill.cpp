#include "dragpill.h"
#include "waylanddrag.h"
#include "theme.h"
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
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#include <QByteArray>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QVariant>
#include <utility>

namespace eddy {

namespace {

QString canonicalOrAbsolute(const QString &path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

class FileBackedMimeData final : public QMimeData {
public:
    FileBackedMimeData(QString path, QString mimeType)
        : m_path(canonicalOrAbsolute(std::move(path))),
          m_mimeType(std::move(mimeType)) {
        setUrls({ QUrl::fromLocalFile(m_path) });
    }

    QStringList formats() const override {
        QStringList out = QMimeData::formats();
        if (!out.contains(m_mimeType))
            out.prepend(m_mimeType);
        return out;
    }

    bool hasFormat(const QString &mimeType) const override {
        return mimeType == m_mimeType || QMimeData::hasFormat(mimeType);
    }

protected:
    QVariant retrieveData(const QString &mimeType, QMetaType preferredType) const override {
        if (mimeType == m_mimeType) {
            QFile f(m_path);
            if (!f.open(QIODevice::ReadOnly))
                return QByteArray();
            return f.readAll();
        }
        return QMimeData::retrieveData(mimeType, preferredType);
    }

private:
    QString m_path;
    QString m_mimeType;
};

}

QMimeData *makeUrlDropMime(const QString &path) {
    auto *mime = new QMimeData;
    mime->setUrls({ QUrl::fromLocalFile(canonicalOrAbsolute(path)) });
    return mime;
}

QMimeData *makeFileDropMime(const QString &path, const QString &mimeType) {
    return new FileBackedMimeData(path, mimeType.isEmpty()
        ? QStringLiteral("application/octet-stream")
        : mimeType);
}

// Mirrors boltsnap's drag payload: explicit image/png bytes on demand (for
// chat/browser/editor targets that look for that exact type) + a text/uri-list
// pointing to a real PNG file (for file-drop targets). setImageData adds
// application/x-qt-image for Qt-native apps.
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
            const QString abs = canonicalOrAbsolute(tmp.fileName());
            mime->setUrls({ QUrl::fromLocalFile(abs) });        // text/uri-list
            if (tempPathOut) *tempPathOut = abs;
        } else {
            const QString p = tmp.fileName();
            tmp.close();
            QFile::remove(p);
        }
    }
    return mime;
}

QPixmap makeDragGhostPixmap(const QImage &preview, const QSize &maxSize) {
    if (preview.isNull() || maxSize.isEmpty())
        return {};

    constexpr int kPad = 8;
    const QSize contentMax(qMax(1, maxSize.width() - kPad * 2),
                           qMax(1, maxSize.height() - kPad * 2));
    const QImage scaled = preview.scaled(contentMax, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QSize canvasSize(qMin(maxSize.width(), scaled.width() + kPad * 2),
                           qMin(maxSize.height(), scaled.height() + kPad * 2));
    QPixmap pix(canvasSize);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF card(kPad / 2.0, kPad / 2.0,
                      canvasSize.width() - kPad, canvasSize.height() - kPad);
    QPainterPath rounded;
    rounded.addRoundedRect(card, 10, 10);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawRoundedRect(card.translated(0, 2), 10, 10);
    p.setBrush(QColor(24, 24, 24, 95));
    p.drawRoundedRect(card, 10, 10);

    p.save();
    p.setClipPath(rounded);
    p.setOpacity(0.72);
    const QPointF imagePos(card.center().x() - scaled.width() / 2.0,
                           card.center().y() - scaled.height() / 2.0);
    p.drawImage(imagePos, scaled);
    p.restore();
    p.end();
    return pix;
}

DragPill::DragPill(QWidget *parent) : QWidget(parent) {
    setObjectName("DragPill");
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::OpenHandCursor);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 6, 14, 6);
    lay->setSpacing(8);
    auto *icon = new QLabel(this);
    icon->setObjectName("DragPillIcon");
    icon->setPixmap(theme::tintedIcon(QStringLiteral(":/icons/dragout.svg"),
                                      QColor("#d0d0d0"), QColor("#d0d0d0"))
                        .pixmap(QSize(16, 16)));   // ↗ box-arrow, boltsnap-style
    lay->addWidget(icon);
    auto *label = new QLabel(QStringLiteral("Drag out"), this);
    label->setObjectName("DragPillText");
    lay->addWidget(label);
}

DragPill::~DragPill() {
    if (!m_lastTempPath.isEmpty()) QFile::remove(m_lastTempPath);
}

void DragPill::setImageProvider(std::function<QImage()> provider) { m_provider = std::move(provider); }
void DragPill::setFileProvider(std::function<FileDragPayload()> provider) { m_fileProvider = std::move(provider); }

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
    QImage preview;
    bool clipboardFallbackImage = false;
    bool removeAfterUse = true;
    bool includeBytesInQtMime = true;
    QString concreteMimeType;
    QString path;
    QMimeData *mime = nullptr;

    if (m_fileProvider) {
        const FileDragPayload payload = m_fileProvider();
        if (payload.path.isEmpty()) return;
        path = canonicalOrAbsolute(payload.path);
        preview = payload.preview;
        removeAfterUse = payload.removeAfterUse;
        includeBytesInQtMime = payload.includeBytesInQtMime;
        concreteMimeType = payload.mimeType;
    } else {
        if (!m_provider) return;
        const QImage img = m_provider();
        if (img.isNull()) return;
        preview = img;
        clipboardFallbackImage = true;
        removeAfterUse = false;
        mime = makeImageDropMime(img, &path);
        concreteMimeType = QStringLiteral("image/png");
    }

    if (!m_lastTempPath.isEmpty()) QFile::remove(m_lastTempPath);   // keep at most one temp file
    m_lastTempPath = removeAfterUse ? path : QString();

    const QPixmap ghost = preview.isNull() ? QPixmap() : makeDragGhostPixmap(preview);

    setCursor(Qt::ClosedHandCursor);
    if (!path.isEmpty() && !concreteMimeType.isEmpty()
        && startWaylandFileDrag(this, path, {concreteMimeType, QStringLiteral("text/uri-list")}, ghost)) {
        delete mime;
        setCursor(Qt::OpenHandCursor);
        return;
    }

    if (!mime) {
        mime = (!includeBytesInQtMime || concreteMimeType.isEmpty())
            ? makeUrlDropMime(path)
            : makeFileDropMime(path, concreteMimeType);
    }
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);                                       // QDrag takes ownership
    if (!ghost.isNull()) {
        drag->setPixmap(ghost);
        drag->setHotSpot(QPoint(qMin(24, ghost.width() / 3), qMin(24, ghost.height() / 3)));
    }
    const Qt::DropAction result = drag->exec(Qt::CopyAction);
    if (result == Qt::IgnoreAction && clipboardFallbackImage)   // dropped on nothing: don't waste it
        QGuiApplication::clipboard()->setImage(preview);        // (boltsnap's image cancel fallback)
    setCursor(Qt::OpenHandCursor);
}

}
