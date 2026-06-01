#include "editorwindow.h"
#include "canvas.h"
#include "toolbar.h"
#include "toolcontroller.h"
#include "exporter.h"
#include "selectionhandles.h"
#include "undocommands.h"
#include "items/textitem.h"
#include "redactbar.h"
#include "toast.h"
#include "redactocrcontroller.h"
#include "items/redactitem.h"
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <cstdio>
#include <QGraphicsPixmapItem>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>
#include <QDir>
#include <QDateTime>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QMouseEvent>
#include <QResizeEvent>

namespace eddy {

EditorWindow::EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : QWidget(parent), m_bg(image), m_cfg(cfg), m_cli(cli) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowTitle("eddy");
    setObjectName("EditorRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(0,0,m_bg.width(),m_bg.height());
    auto *bgItem = m_scene->addPixmap(QPixmap::fromImage(m_bg));
    bgItem->setZValue(-1000);

    m_undo = new QUndoStack(this);
    m_tools = new ToolController(m_scene, m_undo, m_bg, this);
    m_tools->setTool(toolFromName(cfg.defaultTool));
    m_tools->setColor(cfg.strokeColor);
    m_tools->setWidth(cfg.lineWidth);

    m_canvas = new Canvas(m_scene, m_tools, this);
    m_toolbar = new Toolbar(this);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0); lay->setSpacing(0);
    lay->addWidget(m_toolbar);
    lay->addWidget(m_canvas, 1);

    connect(m_toolbar, &Toolbar::toolChosen, m_tools, &ToolController::setTool);
    connect(m_toolbar, &Toolbar::colorChosen, m_tools, &ToolController::setColor);
    connect(m_toolbar, &Toolbar::saveRequested, this, &EditorWindow::save);
    connect(m_toolbar, &Toolbar::copyRequested, this, &EditorWindow::copy);
    connect(m_tools, &ToolController::toolChanged, m_toolbar, &Toolbar::syncTool);
    connect(m_toolbar, &Toolbar::widthChosen, m_tools, &ToolController::setWidth);
    connect(m_toolbar, &Toolbar::undoRequested, this, &EditorWindow::doUndo);
    connect(m_toolbar, &Toolbar::redoRequested, this, &EditorWindow::doRedo);
    connect(m_undo, &QUndoStack::canUndoChanged, m_toolbar, &Toolbar::setUndoEnabled);
    connect(m_undo, &QUndoStack::canRedoChanged, m_toolbar, &Toolbar::setRedoEnabled);
    m_handles = new SelectionHandles(m_scene, m_undo, this);
    m_ocr = new RedactOcrController(m_bg, {m_cfg.ocrLang, m_cfg.ocrPsm}, this);
    m_redactBar = new RedactBar(m_canvas->viewport());
    m_redactBar->hide();
    m_toast = new Toast(this);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshRedactBar);
    connect(m_scene, &QGraphicsScene::changed, this, [this](const QList<QRectF> &){ positionRedactBar(); });
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionRedactBar);
    connect(m_redactBar, &RedactBar::modeChosen, this, &EditorWindow::onRedactModeChosen);
    connect(m_ocr, &RedactOcrController::noTextDetected, this,
            [this]{ m_toast->showMessage(QStringLiteral("No text detected")); });
    connect(m_ocr, &RedactOcrController::ocrFailed, this,
            [this](const QString &msg){ m_toast->showMessage(QStringLiteral("OCR failed: ") + msg); });
    connect(m_handles, &SelectionHandles::resizeFinished, this, [this](QGraphicsItem *it){
        if (auto *r = dynamic_cast<RedactItem*>(it); r && RedactItem::isOcr(r->mode()))
            m_ocr->detectFor(r);   // re-run detection for the new geometry
    });
    m_canvas->setAnimationsEnabled(cfg.animations);
    m_toolbar->setAnimationsEnabled(cfg.animations);
    m_tools->setAnimationsEnabled(cfg.animations);
    // Sync the toolbar to the configured default explicitly: the earlier
    // m_tools->setTool() ran before this connect existed (its toolChanged was
    // dropped), and setTool only emits on a *change* — so a default of "arrow"
    // (the controller's default) would emit nothing at all. Keep this call.
    m_toolbar->syncTool(toolFromName(cfg.defaultTool));
    if (cfg.animations) setWindowOpacity(0.0);   // entrance fade starts transparent

    // size to image, capped
    const int maxW = 1700, maxH = 1000;
    resize(qMin(m_bg.width(), maxW), qMin(m_bg.height()+40, maxH));
    m_toolbar->adjustSize();
    const int barW = m_toolbar->sizeHint().width();
    const int barH = m_toolbar->sizeHint().height();
    setMinimumSize(qMax(barW, 360), barH + 120);   // bar never clipped; usable image strip
}

void EditorWindow::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (m_shown) return;
    m_shown = true;
    if (!m_cfg.animations) { setWindowOpacity(1.0); return; }
    auto *a = new QPropertyAnimation(this, "windowOpacity", this);
    a->setDuration(150); a->setStartValue(0.0); a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void EditorWindow::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    updateCompactMode();
}

void EditorWindow::updateCompactMode() {
    const int barH = m_toolbar->sizeHint().height();
    const bool compact = height() < barH + 90;       // too short for strip + image
    if (compact == m_compact) return;
    m_compact = compact;
    auto *lay = static_cast<QVBoxLayout*>(layout());
    if (compact) {
        // Detach the bar from the layout (removeWidget — setParent alone leaves it
        // managed) and float it as a top auto-hide overlay.
        lay->removeWidget(m_toolbar);
        m_toolbar->setParent(this);                   // keep it a child for geometry/raise
        m_toolbar->raise();
        m_toolbar->setGeometry(0, 0, width(), barH);
        m_toolbar->hide();                            // canvas gets full height; bar on hover
        setMouseTracking(true);
        m_canvas->setMouseTracking(true);
    } else {
        // Re-dock the bar at the top of the layout.
        lay->insertWidget(0, m_toolbar);
        m_toolbar->show();
        setMouseTracking(false);
        m_canvas->setMouseTracking(false);
    }
}

void EditorWindow::mouseMoveEvent(QMouseEvent *e) {
    if (m_compact) {
        const int barH = m_toolbar->sizeHint().height();
        if (e->position().y() <= 12) {
            m_toolbar->setGeometry(0, 0, width(), barH);
            m_toolbar->raise(); m_toolbar->show();
        } else if (e->position().y() > barH + 8) {
            m_toolbar->hide();
        }
    }
    QWidget::mouseMoveEvent(e);
}

RedactItem *EditorWindow::selectedRedact() const {
    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return nullptr;
    return dynamic_cast<RedactItem *>(sel.first());
}

void EditorWindow::refreshRedactBar() {
    RedactItem *r = selectedRedact();
    if (!r) { m_redactBar->hide(); return; }
    m_redactBar->setMode(r->mode());
    m_redactBar->adjustSize();
    m_redactBar->show();
    m_redactBar->raise();
    positionRedactBar();
}

void EditorWindow::positionRedactBar() {
    if (m_redactBar->isHidden()) return;
    RedactItem *r = selectedRedact();
    if (!r) { m_redactBar->hide(); return; }
    const QRectF rc = r->rect();
    const QPoint topCenter = m_canvas->mapFromScene(QPointF(rc.center().x(), rc.top()));
    const QSize vp = m_canvas->viewport()->size();
    int x = topCenter.x() - m_redactBar->width() / 2;
    int y = topCenter.y() - m_redactBar->height() - 8;
    x = qBound(4, x, qMax(4, vp.width() - m_redactBar->width() - 4));
    if (y < 4) y = topCenter.y() + 8;     // no room above -> just below the top edge
    m_redactBar->move(x, y);
}

void EditorWindow::onRedactModeChosen(RedactMode m) {
    RedactItem *r = selectedRedact();
    if (!r) return;
    const RedactMode before = r->mode();
    if (before != m) m_undo->push(new SetRedactModeCommand(r, before, m));
    if (RedactItem::isOcr(m)) m_ocr->detectFor(r);   // detecting -> whole-region cover until result
    m_redactBar->setMode(m);
    positionRedactBar();
}

void EditorWindow::doUndo() { m_ocr->cancel(); m_undo->undo(); refreshRedactBar(); }
void EditorWindow::doRedo() { m_ocr->cancel(); m_undo->redo(); refreshRedactBar(); }

QImage EditorWindow::exportComposite() {
    m_scene->clearSelection();          // drop selection handles so they aren't baked into the image
    return renderToImage(*m_scene, m_bg.size());
}

void EditorWindow::save() {
    QImage img = exportComposite();
    // Write a file only when an explicit target was requested: -o FILE, -o -,
    // or --save-dir DIR. The default (Enter) is clipboard-only — no file is
    // dropped into ~/Pictures unless the user asked for it.
    QString path;
    if (m_cli.output.toFile)            path = m_cli.output.filePath;
    else if (m_cli.output.toStdout)     path = QStringLiteral("-");
    else if (!m_cli.output.saveDir.isEmpty())
        path = QDir(m_cli.output.saveDir).filePath(
                   "eddy-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png");

    if (!path.isEmpty()) {
        auto res = writePng(img, path);
        if (!res.ok) std::fprintf(stderr, "eddy: %s\n", qPrintable(res.error));
    }
    if (m_cfg.copyOnSave || path.isEmpty())
        QApplication::clipboard()->setImage(img);   // always at least copy
    if (m_cfg.earlyExit) close();
}

void EditorWindow::copy() {
    QApplication::clipboard()->setImage(exportComposite());
}

void EditorWindow::keyPressEvent(QKeyEvent *e) {
    // While a text annotation is being edited, don't hijack letter keys as tool
    // hotkeys — let them type into the text box. Esc commits and leaves editing.
    QGraphicsItem *fi = m_scene->focusItem();
    if (fi && fi->type() == TextItem::Type
        && (static_cast<QGraphicsTextItem *>(fi)->textInteractionFlags() & Qt::TextEditorInteraction)) {
        if (e->key() == Qt::Key_Escape) { m_scene->clearFocus(); e->accept(); return; }
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
        case Qt::Key_A: m_tools->setTool(ToolType::Arrow); break;
        case Qt::Key_P: m_tools->setTool(ToolType::Pen); break;
        case Qt::Key_R: m_tools->setTool(ToolType::Rect); break;
        case Qt::Key_E: m_tools->setTool(ToolType::Ellipse); break;
        case Qt::Key_H: m_tools->setTool(ToolType::Highlight); break;
        case Qt::Key_T: m_tools->setTool(ToolType::Text); break;
        case Qt::Key_X: m_tools->setTool(ToolType::Redact); break;
        case Qt::Key_M: m_tools->setTool(ToolType::Move); break;
        case Qt::Key_Z: if (e->modifiers() & Qt::ControlModifier) {
                            (e->modifiers() & Qt::ShiftModifier) ? doRedo() : doUndo();
                        } break;
        case Qt::Key_C: if (e->modifiers() & Qt::ControlModifier) copy(); break;
        case Qt::Key_S: if (e->modifiers() & Qt::ControlModifier) save(); break;
        case Qt::Key_Return: case Qt::Key_Enter: save(); break;
        case Qt::Key_Delete: case Qt::Key_Backspace: {
            const auto sel = m_scene->selectedItems();
            bool removed = false;
            for (QGraphicsItem *it : sel) {
                if (it->zValue() <= -1000) continue;     // never the background
                if (auto *r = dynamic_cast<RedactItem *>(it)) m_ocr->forget(r);
                m_undo->push(new RemoveItemCommand(m_scene, it));
                removed = true;
            }
            if (!removed) QWidget::keyPressEvent(e);
            break;
        }
        case Qt::Key_Escape: close(); break;
        default: QWidget::keyPressEvent(e);
    }
}

}
