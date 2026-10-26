#include "editor/editorwindow.h"
#include "bars/stepbar.h"
#include "bars/selectionbar.h"
#include "canvas/snapping.h"
#include "items/labels/stepitem.h"
#include "timeline/audiowaveform.h"
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
#include "timeline/zoomlane.h"
#include "timeline/zoombar.h"
#include "timeline/fragmentbar.h"
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

void EditorWindow::scheduleVideoLoad() {
    if (!isVideo() || m_videoLoadQueued || m_player) return;
    m_videoLoadQueued = true;
    // Small delay lets the frameless editor paint before Qt Multimedia opens the
    // file. The scene dimensions are already known from ffprobe, so no UX state
    // depends on synchronously constructing the backend.
    QTimer::singleShot(75, this, [this]{
        m_videoLoadQueued = false;
        ensureVideoPlayer();
    });
}

void EditorWindow::togglePlayback() {
    ensureVideoPlayer();
    if (!m_player) return;
    if (m_timelineActive) return;
    if (m_seekSettling) { m_resumeAfterSeek = !m_resumeAfterSeek; return; }
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        if (m_player->position() < m_trimInMs || m_player->position() >= m_trimOutMs)
            m_player->setPosition(m_trimInMs);
        m_player->play();
    }
}

void EditorWindow::handlePlaybackEnd() {
    if (!m_player || m_loopSeeking || m_timelineActive || m_seekSettling) return;
    m_player->pause();
    m_loopSeeking = true;
    m_resumeAfterSeek = m_loopButton->isChecked();
    const qint64 lastFrame = m_media.video.fps > 0
        ? qRound64((qCeil(m_trimOutMs * m_media.video.fps / 1000.0) - 1) * 1000.0 / m_media.video.fps)
        : m_trimOutMs - 1;
    requestVideoSeek(m_resumeAfterSeek ? m_trimInMs : qMax(m_trimInMs, lastFrame));
    m_timeline->setPosition(m_seekTarget);
    flushVideoSeek();
}

bool EditorWindow::eventFilter(QObject *object, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || (widget != this && !isAncestorOf(widget)))
        return QWidget::eventFilter(object, event);
    if (event->type() == QEvent::KeyPress && m_crop && m_crop->active()
        && !qobject_cast<QMenu *>(widget) && !qobject_cast<QLineEdit *>(widget)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            if (m_crop->dragging()) m_crop->cancelDrag(); else m_crop->cancel();
            return true;
        }
        if (m_crop->dragging() && ((key->modifiers().testFlag(Qt::ControlModifier)
            && (key->key() == Qt::Key_C || key->key() == Qt::Key_S))
            || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
            return true;
    }
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        auto *button = qobject_cast<QToolButton *>(widget);
        if (button && button->isEnabled() && button->menu()
            && key->key() == Qt::Key_Down && key->modifiers() == Qt::AltModifier) {
            m_tooltip->hide();
            if (!key->isAutoRepeat()) button->showMenu();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress && (widget == m_trimInLabel || widget == m_trimOutLabel)) {
        const auto *key = static_cast<QKeyEvent *>(event);
        auto *field = static_cast<QLineEdit *>(widget);
        if (key->key() == Qt::Key_Escape) {
            field->setText(formatPreciseTime(field == m_trimInLabel ? m_trimInMs : m_trimOutMs));
            field->setModified(false);
            m_canvas->setFocus();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            commitTrimTime(field);
            return true;
        }
    }
    if (event->type() == QEvent::ToolTip && !widget->toolTip().isEmpty()) {
        if (widget == m_timeline) return true; // The timeline has its own image/time hint.
        const auto *help = static_cast<QHelpEvent *>(event);
        m_tooltip->setText(theme::tooltipHtml(widget->toolTip()));
        m_tooltip->adjustSize();
        QPoint pos = widget->mapTo(this, help->pos()) + QPoint(10, 18);
        if (pos.y() + m_tooltip->height() > height() - 4)
            pos.setY(widget->mapTo(this, help->pos()).y() - m_tooltip->height() - 8);
        pos.setX(qBound(4, pos.x(), qMax(4, width() - m_tooltip->width() - 4)));
        pos.setY(qBound(4, pos.y(), qMax(4, height() - m_tooltip->height() - 4)));
        m_tooltip->move(pos);
        m_tooltipOwner = widget;
        m_tooltip->show();
        m_tooltip->raise();
        m_tooltipTimer->start(5000);
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) m_spaceConsumed = true;
    if (event->type() == QEvent::FocusOut) {
        m_spaceArmed = false;
        m_canvas->cancelPan();
    }
    if (event->type() == QEvent::WindowDeactivate && widget == this) {
        if (m_crop && m_crop->active()) m_crop->release();
        m_spaceArmed = false;
        m_canvas->cancelPan();
        if (m_timeline) m_timeline->cancelInteraction();
        hideVideoPreview();
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress
        || event->type() == QEvent::WindowDeactivate
        || (event->type() == QEvent::Leave && widget == m_tooltipOwner))
        m_tooltip->hide();
    return QWidget::eventFilter(object, event);
}

void EditorWindow::scheduleContactSheetLoad() {
    if (!m_previewProvider || !isVisible()) return;
    m_previewProvider->requestStrip(m_timeline->thumbnailTimes(), QSize(128, 72) * devicePixelRatioF());
}

void EditorWindow::hideVideoPreview() {
    if (!m_videoPreview) return;
    m_hoverTime = -1;
    m_previewSampleTime = -1;
    m_previewTimer->stop();
    m_videoPreview->hide();
    m_previewProvider->cancelHover();
}

void EditorWindow::showVideoPreview() {
    if (m_hoverTime < 0 || !isVisible()) return;
    const bool appearing = m_videoPreview->isHidden();
    // The nearest decoded frame, when it differs, follows a step quieter.
    m_previewTime->setText(m_previewSampleTime >= 0 && m_previewSampleTime != m_hoverTime
        ? QStringLiteral("%1&nbsp;&nbsp;<span style=\"color:%2\">~%3</span>")
              .arg(formatPreciseTime(m_hoverTime), palette().color(QPalette::PlaceholderText).name(),
                   formatPreciseTime(m_previewSampleTime))
        : formatPreciseTime(m_hoverTime));
    m_videoPreview->adjustSize();
    QPoint pos = m_timeline->mapTo(this, m_hoverPoint);
    pos.setX(qBound(4, pos.x() - m_videoPreview->width() / 2,
                    qMax(4, width() - m_videoPreview->width() - 4)));
    pos.setY(qMax(4, m_timeline->mapTo(this, QPoint()).y() - m_videoPreview->height() - 5));
    m_videoPreview->move(pos);
    m_videoPreview->show();
    m_videoPreview->raise();
    if (appearing && m_cfg.animations) m_previewFade->start();
    m_previewProvider->requestHover(m_hoverTime, QSize(256, 144) * devicePixelRatioF());
}

void EditorWindow::setVideoPreviewImage(const QImage &image) {
    QPixmap pixmap = QPixmap::fromImage(image).scaled(m_previewImage->size() * devicePixelRatioF(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_previewImage->setPixmap(pixmap);
}

bool EditorWindow::updateVideoBackground() {
    if (m_videoBackgroundCurrent) return true;
    const auto &frame = m_lastVideoFrame;
    QImage image = frame.toImage();
    if (image.isNull()) return false;
    // Qt < 6.8 includes rotation/mirroring in toImage(). Newer Qt
    // applies the surface transform there, but not the presentation one.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const int rotation = int(frame.rotation());
    if (rotation) image = image.transformed(QTransform().rotate(rotation));
    if (frame.mirrored()) image = image.transformed(QTransform().scale(-1, 1));
#endif
    if (image.size() != m_media.nativeSize())
        image = image.scaled(m_media.nativeSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_bg = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_tools->setBackground(m_bg);
    m_ocr->setBackground(m_bg);
    for (QGraphicsItem *item : m_scene->items())
        if (auto *redact = dynamic_cast<RedactItem *>(item))
            redact->setSource(m_bg);
    m_videoBackgroundCurrent = true;
    return true;
}

void EditorWindow::showVideoStill() {
    if (!m_lastVideoFrame.isValid() || !updateVideoBackground()) return;
    m_videoStill->setPixmap(QPixmap::fromImage(m_bg));
    m_videoStill->show();
}

void EditorWindow::ensureVideoPlayer() {
    if (!isVideo() || m_player) return;
    if (!m_videoItem) {
        auto *videoItem = new PreviewVideoItem;
        videoItem->setSize(QSizeF(m_media.nativeSize()));
        videoItem->setAspectRatioMode(Qt::IgnoreAspectRatio);
        videoItem->setZValue(-1000);
        m_scene->addItem(videoItem);
        if (m_backgroundItem) {
            m_scene->removeItem(m_backgroundItem);
            delete m_backgroundItem;
        }
        m_videoItem = videoItem;
        m_backgroundItem = m_videoItem;
        // Keep paused frames independent of the backend's video surface. Making
        // the still a background child also excludes it from annotation exports.
        m_videoStill = new PreviewPixmapItem(videoItem);
        m_videoStill->setZValue(-1000);
        m_videoStill->setAcceptedMouseButtons(Qt::NoButton);
        m_videoStill->setTransformationMode(Qt::SmoothTransformation);
        connect(videoItem->videoSink(), &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) {
            if (!frame.isValid()) return;
            m_lastVideoFrame = frame;
            m_videoBackgroundCurrent = false;
            bool needsPixels = m_tools->tool() == ToolType::Redact;
            if (!needsPixels)
                for (QGraphicsItem *item : m_scene->items())
                    if (dynamic_cast<RedactItem *>(item)) { needsPixels = true; break; }
            if (needsPixels) updateVideoBackground();
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_videoStill->hide();
            else
                showVideoStill();
            // GStreamer's buffer PTS may include a stream offset (e.g. H.264
            // reordering delay), while QMediaPlayer positions start at zero.
            // Loading primes the initial still. Anchor that first frame before
            // any explicit seek; the asynchronous player clock may already tick.
            if (!m_hasVideoFrame && !m_hasSentVideoSeek && frame.startTime() >= 0)
                m_frameTimeOrigin = frame.startTime() / 1000;
            m_hasVideoFrame = true;
            m_presentedStart = frame.startTime() < 0 ? -1 : frame.startTime() / 1000 - m_frameTimeOrigin;
            m_presentedEnd = frame.endTime() < 0 ? -1 : frame.endTime() / 1000 - m_frameTimeOrigin;
            if (!m_timelineActive && !m_seekSettling && m_presentedStart >= 0
                && m_player->playbackState() == QMediaPlayer::PlayingState)
                m_timeline->setPosition(m_presentedStart);
            if (m_seekSettling && m_presentedStart >= 0 && m_seekTarget >= m_presentedStart - 1
                && m_seekTarget < (m_presentedEnd > m_presentedStart ? m_presentedEnd
                    : m_presentedStart + qMax<qint64>(1, qRound64(1000 / qMax(1.0, m_media.video.fps)))))
                finishVideoSeek();
            updateCamera();
            // Paused, the playhead is what was asked for; a frame may start a little before it.
            if (m_player->playbackState() != QMediaPlayer::PlayingState) applyTimeWindows(m_timeline->position());
            else if (m_presentedStart >= 0) applyTimeWindows(m_presentedStart);
            if (m_presentedStart >= 0 && m_player->playbackState() == QMediaPlayer::PlayingState)
                followPlaybackPieces(m_presentedStart);
            if (m_miniMap && m_miniMap->isVisible() && updateVideoBackground())
                m_miniMap->setContent(m_bg.copy(cameraContent())
                    .scaled(304, 224, Qt::KeepAspectRatio, Qt::SmoothTransformation), QRectF(cameraContent()));
        });
    }
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_audioOutput->setVolume(m_volumeSlider ? m_volumeSlider->value() / 100.0f : 1.0f);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoItem);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state){
        if (state != QMediaPlayer::PlayingState) showVideoStill();
        updateCamera();
        refreshZoomUi();
        if (m_playButton) {
            const QColor color = palette().color(QPalette::ButtonText);
            m_playButton->setIcon(theme::tintedIcon(
                state == QMediaPlayer::PlayingState
                    ? QStringLiteral(":/icons/pause.svg") : QStringLiteral(":/icons/play.svg"),
                color, color));
        }
    });
    connect(m_player, &QMediaPlayer::playbackRateChanged, this, [this](qreal) {
        // The label shows the preview rate; a fragment's own speed multiplies it.
        theme::setMenuLabel(m_speedButton, QStringLiteral("%1×").arg(m_previewRate));
        for (auto *action : m_speedButton->menu()->actions())
            action->setChecked(qFuzzyCompare(action->data().toDouble(), m_previewRate));
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) handlePlaybackEnd();
        if (status == QMediaPlayer::LoadedMedia) {
            // Retry after loading for backends that cannot preroll during setSource.
            // Queue it so a pending user play/seek request takes precedence.
            QTimer::singleShot(0, this, [this] {
                if (!m_hasVideoFrame && !m_hasSentVideoSeek
                    && m_player->playbackState() == QMediaPlayer::StoppedState)
                    m_player->pause();
            });
        }
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration){
        if (!m_timeline) return;
        const bool usedFullRange = m_trimInMs == 0 && m_trimOutMs == m_timeline->duration();
        m_timeline->setDuration(duration);
        if (usedFullRange) {
            m_timeline->setTrimRange(0, duration);
            m_trimOutMs = duration;
        }
        m_media.video.durationMs = duration;
        m_trimInMs = m_timeline->trimIn();
        m_trimOutMs = m_timeline->trimOut();
        updateTrimTimeLabels(m_trimInMs, m_trimOutMs);
        rebuildCamera();
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(m_player->position()) + QStringLiteral(" / ") + formatTime(duration));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos){
        if (m_timelineActive || m_seekSettling) return;
        if (m_timeline && (m_presentedStart < 0 || m_player->playbackState() != QMediaPlayer::PlayingState))
            m_timeline->setPosition(pos);
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(pos) + QStringLiteral(" / ") + formatTime(m_player->duration()));
        if (m_player->playbackState() == QMediaPlayer::PlayingState) followPlaybackPieces(pos);
        if (m_player->playbackState() == QMediaPlayer::PlayingState && pos >= m_trimOutMs) {
            handlePlaybackEnd();
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState && m_hoverTime < 0
            && (pos < m_timeline->visibleStart() || pos > m_timeline->visibleEnd()))
            m_timeline->ensureVisible(pos);
    });
    connect(m_player, &QMediaPlayer::seekableChanged, this, [this](bool seekable) {
        if (seekable && m_seekTarget >= 0) flushVideoSeek();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this] {
        m_seekTimer->stop();
        m_seekSettleTimer->stop();
        m_seekSettling = m_loopSeeking = m_resumeAfterSeek = false;
        m_seekTarget = -1;
        if (m_copyFramePending) {
            m_copyFramePending = false;
            if (m_toast) m_toast->showMessage(tr("Frame unavailable"));
        }
    });
    m_player->setSource(QUrl::fromLocalFile(m_media.path));
    m_player->pause(); // Decode the initial still and establish its time origin before editing.
}

} // namespace eddy
