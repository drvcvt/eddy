#include "editor/editorwindow.h"
#include "app/fills.h"
#include "bars/stepbar.h"
#include "bars/selectionbar.h"
#include "canvas/snapping.h"
#include "items/labels/stepitem.h"
#include "timeline/lanes/audiowaveform.h"
#include "canvas/canvas.h"
#include "edit/cropcontroller.h"
#include "bars/cropbar.h"
#include "chrome/toolbar.h"
#include "edit/toolcontroller.h"
#include "output/exporter.h"
#include "project/boltsnapipc.h"
#include "video/videoexporter.h"
#include "popovers/studiopopover.h"
#include <QFileDialog>
#include "timeline/videotimeline.h"
#include "video/videopreviewprovider.h"
#include "canvas/selectionhandles.h"
#include "edit/undocommands.h"
#include "items/labels/textitem.h"
#include "bars/redactbar.h"
#include "bars/textbar.h"
#include "bars/spotlightbar.h"
#include "items/regions/spotlightitem.h"
#include "chrome/toast.h"
#include "chrome/dragpill.h"
#include "edit/redactocrcontroller.h"
#include "items/regions/redactitem.h"
#include "canvas/previewitems.h"
#include "timeline/lanes/zoomlane.h"
#include "timeline/bars/zoombar.h"
#include "timeline/bars/fragmentbar.h"
#include "video/fragments.h"
#include "studio/zoomsuggest.h"
#include "studio/studiopresets.h"
#include <QInputDialog>
#include "popovers/exportpanel.h"
#include "project/projectstore.h"
#include "project/recoverystore.h"
#include "popovers/resumedialog.h"
#include <QThread>
#include "chrome/minimap.h"
#include "chrome/motionicon.h"
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <cstdio>
#include <QGraphicsPixmapItem>
#include <QGraphicsVideoItem>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QDir>
#include <QDateTime>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QBrush>
#include <QPen>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QUrl>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QHelpEvent>
#include <QLineEdit>
#include <QMenu>
#include <QActionGroup>
#include <cmath>
#include <QtMath>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QWidgetAction>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <limits>
#include <utility>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#endif

#include "editor/editorwindow_p.h"

namespace eddy {

RedactItem *EditorWindow::selectedRedact() const {
    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return nullptr;
    return dynamic_cast<RedactItem *>(sel.first());
}

TextItem *EditorWindow::selectedText() const {
    const auto selected = m_scene->selectedItems();
    return selected.size() == 1 ? dynamic_cast<TextItem *>(selected.first()) : nullptr;
}

void EditorWindow::refreshTextBar() {
    TextItem *text = selectedText();
    if (!text) { fadeVisible(m_textBar, false); return; }
    m_textBar->setState(text->state());
    m_textBar->adjustSize();
    fadeVisible(m_textBar, true);
    m_textBar->raise();
    positionTextBar();
}

void EditorWindow::positionTextBar() {
    if (m_textBar->isHidden()) return;
    TextItem *text = selectedText();
    if (!text) { fadeVisible(m_textBar, false); return; }
    const QRectF bounds = text->sceneBoundingRect();
    const QRect item(m_canvas->mapFromScene(bounds.topLeft()),
                     m_canvas->mapFromScene(bounds.bottomRight()));
    m_textBar->move(contextBarPosition(item.normalized(), m_textBar->size(),
                                       m_canvas->viewport()->size()));
}

void EditorWindow::updateSelectedText(const std::function<void(TextItem *)> &change) {
    TextItem *text = selectedText();
    if (!text) return;
    const TextState before = text->state();
    change(text);
    const TextState after = text->state();
    if (!(after == before) && m_tools->editingText() != text)
        m_undo->push(new EditTextCommand(text, before, after));
    refreshTextBar();
}

// Selected items that can move, in a stable order: the stacking order.
QList<QGraphicsItem *> EditorWindow::alignableSelection() const {
    QList<QGraphicsItem *> out;
    for (QGraphicsItem *item : m_scene->items(Qt::AscendingOrder))
        if (item->isSelected() && item->flags().testFlag(QGraphicsItem::ItemIsMovable)) out.append(item);
    return out;
}

void EditorWindow::refreshSelectionBar() {
    const QList<QGraphicsItem *> items = alignableSelection();
    if (items.size() < 2 || m_tools->editingText()) { fadeVisible(m_selectionBar, false); return; }
    QVector<QRectF> boxes;
    for (QGraphicsItem *item : items) boxes.append(alignmentBounds(item));
    m_selectionBar->setSelection(items.size(), !distributeDeltas(boxes, Qt::Horizontal).isEmpty(),
                                 !distributeDeltas(boxes, Qt::Vertical).isEmpty());
    m_selectionBar->adjustSize();
    fadeVisible(m_selectionBar, true);
    m_selectionBar->raise();
    positionContextBars();
}

// Positions only, as one undo step; moves within a hundredth of a pixel are none.
void EditorWindow::moveSelection(const QVector<QPointF> &deltas, const QString &name) {
    const QList<QGraphicsItem *> items = alignableSelection();
    if (deltas.size() != items.size()) return;
    QList<QGraphicsItem *> moved;
    QList<QPointF> before, after;
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (std::abs(deltas[i].x()) < 0.01 && std::abs(deltas[i].y()) < 0.01) continue;
        moved.append(items[i]);
        before.append(items[i]->pos());
        after.append(items[i]->pos() + deltas[i]);
    }
    if (moved.isEmpty()) return;
    auto *command = new MoveItemsCommand(moved, before, after);
    command->setText(name);
    m_undo->push(command);
}

StepItem *EditorWindow::selectedStep() const {
    const auto selected = m_scene->selectedItems();
    return selected.size() == 1 ? dynamic_cast<StepItem *>(selected.first()) : nullptr;
}

void EditorWindow::refreshStepBar() {
    StepItem *step = selectedStep();
    if (!step) { fadeVisible(m_stepBar, false); return; }
    m_stepBar->setStep(step->number(), step->size());
    m_stepBar->adjustSize();
    fadeVisible(m_stepBar, true);
    m_stepBar->raise();
    positionContextBars();
}

// The step and selection bars follow their items through drags, zoom and pan.
void EditorWindow::positionContextBars() {
    auto place = [this](QWidget *bar, const QRectF &bounds) {
        const QRect area(m_canvas->mapFromScene(bounds.topLeft()), m_canvas->mapFromScene(bounds.bottomRight()));
        bar->move(contextBarPosition(area.normalized(), bar->size(), m_canvas->viewport()->size()));
    };
    if (m_stepBar->isVisible())
        if (StepItem *step = selectedStep()) place(m_stepBar, step->sceneBoundingRect());
    if (m_selectionBar && m_selectionBar->isVisible()) {
        QRectF all;
        for (QGraphicsItem *item : alignableSelection()) all |= alignmentBounds(item);
        place(m_selectionBar, all);
    }
}

// 1, 2, 3 in the order the steps were made, as one undo step.
void EditorWindow::renumberSteps() {
    QList<StepItem *> steps;
    for (QGraphicsItem *item : m_scene->items())
        if (auto *step = dynamic_cast<StepItem *>(item)) steps.append(step);
    std::sort(steps.begin(), steps.end(), [](const StepItem *a, const StepItem *b) { return a->serial() < b->serial(); });
    auto *all = new QUndoCommand(tr("Renumber steps"));
    for (int i = 0; i < steps.size(); ++i)
        if (steps[i]->number() != i + 1)
            new SetStepCommand(steps[i], steps[i]->number(), steps[i]->size(), i + 1, steps[i]->size(), all);
    if (all->childCount() == 0) { delete all; return; }
    m_undo->push(all);
}

SpotlightItem *EditorWindow::selectedSpotlight() const {
    const auto selected = m_scene->selectedItems();
    return selected.size() == 1 ? dynamic_cast<SpotlightItem *>(selected.first()) : nullptr;
}

void EditorWindow::refreshSpotlightBar() {
    SpotlightItem *spotlight = selectedSpotlight();
    if (!spotlight) { fadeVisible(m_spotlightBar, false); return; }
    m_spotlightBar->setValues(spotlight->spotlightShape(), spotlight->intensity());
    m_spotlightBar->setTimeScope(isVideo(), spotlight->timeWindow().has_value());
    m_spotlightBar->adjustSize();
    fadeVisible(m_spotlightBar, true);
    m_spotlightBar->raise();
    positionSpotlightBar();
}

void EditorWindow::positionSpotlightBar() {
    if (m_spotlightBar->isHidden()) return;
    SpotlightItem *spotlight = selectedSpotlight();
    if (!spotlight) { fadeVisible(m_spotlightBar, false); return; }
    const QRectF bounds = spotlight->mapRectToScene(spotlight->rect());
    const QRect item(m_canvas->mapFromScene(bounds.topLeft()),
                     m_canvas->mapFromScene(bounds.bottomRight()));
    m_spotlightBar->move(contextBarPosition(item.normalized(), m_spotlightBar->size(),
                                            m_canvas->viewport()->size()));
}

void EditorWindow::refreshRedactBar() {
    RedactItem *r = selectedRedact();
    if (!r) { fadeVisible(m_redactBar, false); return; }
    m_redactBar->setMode(r->mode());
    m_redactBar->setTimeScope(isVideo(), r->timeWindow().has_value());
    m_redactBar->adjustSize();
    fadeVisible(m_redactBar, true);
    m_redactBar->raise();
    positionRedactBar();
}

void EditorWindow::positionRedactBar() {
    if (m_redactBar->isHidden()) return;
    RedactItem *r = selectedRedact();
    if (!r) { fadeVisible(m_redactBar, false); return; }
    const QRectF bounds = r->mapRectToScene(r->rect());
    const QRect item(m_canvas->mapFromScene(bounds.topLeft()),
                     m_canvas->mapFromScene(bounds.bottomRight()));
    m_redactBar->move(contextBarPosition(item.normalized(), m_redactBar->size(),
                                         m_canvas->viewport()->size()));
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

void EditorWindow::doUndo() { cancelGestures(); m_undo->undo(); refreshRedactBar(); }
void EditorWindow::doRedo() { cancelGestures(); m_undo->redo(); refreshRedactBar(); }

// Undo and redo land on a settled document: no gesture keeps a stale copy.
void EditorWindow::cancelGestures() {
    if (m_crop && m_crop->active()) m_crop->cancel();
    m_ocr->cancel();
    if (m_timeline && (m_timeline->zoomDragging() || m_timeline->marking())) m_timeline->cancelInteraction();
    if (m_canvas->cameraDragging()) m_canvas->cancelCameraDrag();
    finishCameraGesture(true);
}

void EditorWindow::setupCrop() {
    m_crop = new CropController(this);
    m_cropBar = new CropBar(m_canvas->viewport());
    m_cropBar->setSourceSize(m_media.nativeSize());
    m_cropBar->hide();
    m_canvas->setCropController(m_crop);
    connect(m_crop, &CropController::changed, this, [this] {
        if (!m_crop->active()) return;
        m_cropBar->setOutputSize(m_crop->pixels().size());
        positionCropBar();
    });
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionCropBar);
    connect(m_cropBar, &CropBar::ratioChosen, this, [this](qreal ratio) {
        m_crop->setRatio(ratio);
        positionCropBar();
    });
    connect(m_cropBar, &CropBar::resetRequested, this, [this] {
        m_cropBar->resetRatio();
        m_crop->reset();
        positionCropBar();
    });
    connect(m_cropBar, &CropBar::applyRequested, m_crop, &CropController::accept);
    connect(m_cropBar, &CropBar::cancelRequested, m_crop, &CropController::cancel);
    connect(m_crop, &CropController::accepted, this, [this](QRect rect) {
        if (rect == QRect(QPoint(), m_media.nativeSize())) rect = {};
        if (rect != m_cropRect)
            m_undo->push(new SetCropCommand(m_cropRect, rect, [this](QRect value) { setCropRect(value); }));
        else setCropRect(rect);
        fadeVisible(m_cropBar, false);
        m_handles->setVisible(true);
        if (m_tools->tool() == ToolType::Crop) m_tools->setTool(ToolType::Move);
        m_canvas->setFocus();
        if (isVideo() && hasVideoEdits()) scheduleVideoExportCache();
    });
    connect(m_crop, &CropController::cancelled, this, [this] {
        setCropRect(m_cropRect);
        m_canvas->restoreView(m_beforeCropView, m_beforeCropCenter, m_beforeCropFit);
        const auto items = m_scene->items();
        for (auto *item : m_beforeCropSelection)
            if (items.contains(item)) item->setSelected(true);
        fadeVisible(m_cropBar, false);
        m_handles->setVisible(true);
        if (m_tools->tool() == ToolType::Crop) m_tools->setTool(ToolType::Move);
        m_canvas->setFocus();
        if (isVideo() && hasVideoEdits()) scheduleVideoExportCache();
    });
    connect(m_tools, &ToolController::toolChanged, this, [this](ToolType type) {
        if (type != ToolType::Crop) { finishCrop(); return; }
        if (isVideo() && !m_media.video.cropSupported) {
            m_toast->showMessage(tr("Crop is unavailable for this video's display transform"));
            m_tools->setTool(ToolType::Move);
            return;
        }
        m_tools->commitTextEdit();
        m_beforeCropSelection = m_scene->selectedItems();
        m_scene->clearSelection();
        m_scene->clearFocus();
        m_handles->setVisible(false);
        m_canvas->setCamera({});
        refreshZoomUi();
        m_beforeCropView = m_canvas->transform();
        m_beforeCropCenter = m_canvas->mapToScene(m_canvas->viewport()->rect().center());
        m_beforeCropFit = m_canvas->fitted();
        m_canvas->setContentRect({});
        m_cropBar->resetRatio();
        m_crop->begin(m_media.nativeSize(), m_cropRect, isVideo() ? 2 : 1);
        if (m_beforeCropFit) m_canvas->fitMedia();
        fadeVisible(m_cropBar, true);
        positionCropBar();
        m_canvas->setFocus();
    });
    if (m_tools->tool() == ToolType::Crop) {
        m_tools->setTool(ToolType::Move);
        QTimer::singleShot(0, this, [this] { m_tools->setTool(ToolType::Crop); });
    }
}

void EditorWindow::finishCrop() {
    if (m_crop && m_crop->active()) m_crop->accept();
}

void EditorWindow::setCropRect(QRect rect) {
    m_cropRect = rect;
    applyContentRect();
    rebuildCamera();
    const QSize size = rect.isEmpty() ? m_media.nativeSize() : rect.size();
    const QString name = m_media.path.isEmpty() ? QStringLiteral("Image") : QFileInfo(m_media.path).fileName();
    setWindowTitle(QStringLiteral("%1 (%2 × %3)")
        .arg(name).arg(size.width()).arg(size.height()));
}

// The canvas shows the base view: the crop, narrowed by "keep zoomed in".
void EditorWindow::applyContentRect() {
    const QRect base = cameraBase();
    m_canvas->setContentRect(base == QRect(QPoint(), m_media.nativeSize()) ? QRect() : base);
    updateStudioPreview();
}

void EditorWindow::positionCropBar() {
    if (!m_crop || !m_crop->active()) return;
    m_cropBar->setAvailableWidth(m_canvas->viewport()->width() - 12);
    const QRect rect = m_canvas->mapFromScene(m_crop->rect()).boundingRect();
    const QSize viewport = m_canvas->viewport()->size();
    int y = rect.bottom() + 12;
    if (y + m_cropBar->height() > viewport.height() - 6) y = rect.top() - m_cropBar->height() - 12;
    m_cropBar->move(qBound(6, rect.center().x() - m_cropBar->width() / 2,
                          qMax(6, viewport.width() - m_cropBar->width() - 6)),
                    qBound(6, y, qMax(6, viewport.height() - m_cropBar->height() - 6)));
    m_cropBar->raise();
}

void EditorWindow::toggleTheme() {
    m_dark = !m_dark;
    QApplication::setPalette(theme::palette(m_dark));
    qApp->setStyleSheet(theme::styleSheet(m_dark));
    m_canvas->setBackgroundBrush(m_canvas->workspaceColor());
    m_toolbar->setDark(m_dark);
    if (m_playButton) {
        const QColor color = palette().color(QPalette::ButtonText);
        const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
        m_playButton->setIcon(theme::tintedIcon(
            playing ? QStringLiteral(":/icons/pause.svg") : QStringLiteral(":/icons/play.svg"),
            color, color));
        const bool muted = m_audioOutput && m_audioOutput->isMuted();
        m_muteButton->setIcon(theme::tintedIcon(
            muted ? QStringLiteral(":/icons/muted.svg") : QStringLiteral(":/icons/volume.svg"),
            color, color));
        const QColor label = palette().color(QPalette::WindowText);
        if (m_splitButton)
            m_splitButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/scissors.svg"), label, label, theme::kLabelIcon));
        if (m_deleteButton)
            m_deleteButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/trash.svg"), label, label, theme::kLabelIcon));
        if (m_loopButton)
            m_loopButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/loop.svg"), color, color));
        if (m_speedButton) theme::setMenuArrow(m_speedButton);
    }
    m_textBar->refreshTheme();
    m_stepBar->refreshTheme();
    m_selectionBar->refreshTheme();
    m_dragPill->refreshTheme();
    if (m_zoomBar) m_zoomBar->refreshTheme();
    m_scene->update();
#ifdef Q_OS_WIN
    applyWindowsTitleBarTheme(this, m_dark);
#endif

    QSettings settings(m_cli.configPath.isEmpty() ? defaultConfigPath() : m_cli.configPath,
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("eddy"));
    settings.setValue(QStringLiteral("theme"), m_dark ? QStringLiteral("dark")
                                                       : QStringLiteral("light"));
    settings.endGroup();
    m_cfg.theme = m_dark ? ThemeMode::Dark : ThemeMode::Light;
}

} // namespace eddy
