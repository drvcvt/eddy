#include "editorwindow.h"
#include "canvas.h"
#include "toolbar.h"
#include "toolcontroller.h"
#include "exporter.h"
#include "items/textitem.h"
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

namespace eddy {

EditorWindow::EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : QWidget(parent), m_bg(image), m_cfg(cfg), m_cli(cli) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowTitle("eddy");
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
    lay->setContentsMargins(0,0,0,0); lay->setSpacing(0);
    lay->addWidget(m_toolbar);
    lay->addWidget(m_canvas, 1);

    connect(m_toolbar, &Toolbar::toolChosen, m_tools, &ToolController::setTool);
    connect(m_toolbar, &Toolbar::colorChosen, m_tools, &ToolController::setColor);
    connect(m_toolbar, &Toolbar::saveRequested, this, &EditorWindow::save);
    connect(m_toolbar, &Toolbar::copyRequested, this, &EditorWindow::copy);

    // size to image, capped
    const int maxW = 1700, maxH = 1000;
    resize(qMin(m_bg.width(), maxW), qMin(m_bg.height()+40, maxH));
}

QImage EditorWindow::exportComposite() {
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
        case Qt::Key_B: m_tools->setTool(ToolType::Blur); break;
        case Qt::Key_X: m_tools->setTool(ToolType::Redact); break;
        case Qt::Key_M: m_tools->setTool(ToolType::Move); break;
        case Qt::Key_Z: if (e->modifiers() & Qt::ControlModifier) {
                            (e->modifiers() & Qt::ShiftModifier) ? m_undo->redo() : m_undo->undo();
                        } break;
        case Qt::Key_C: if (e->modifiers() & Qt::ControlModifier) copy(); break;
        case Qt::Key_S: if (e->modifiers() & Qt::ControlModifier) save(); break;
        case Qt::Key_Return: case Qt::Key_Enter: save(); break;
        case Qt::Key_Escape: close(); break;
        default: QWidget::keyPressEvent(e);
    }
}

}
