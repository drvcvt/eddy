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
#include <QThreadPool>
#include <utility>
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
        // Parked on the last frame (where playback ends) or outside the range: from the top.
        if (m_player->position() < m_trimInMs || m_player->position() >= lastFrameMs())
            m_player->setPosition(m_trimInMs);
        m_player->play();
    }
}

// Where playback ends: the trim's end, or earlier where a cut runs up to it;
// it parks on the frame before.
qint64 EditorWindow::lastFrameMs() const {
    const QVector<TimePiece> &pieces = m_timeMap.pieces();
    const qint64 end = pieces.isEmpty() ? m_trimOutMs : qMin(m_trimOutMs, qRound64(pieces.last().srcEnd));
    return m_media.video.fps > 0
        ? qRound64((qCeil(end * m_media.video.fps / 1000.0) - 1) * 1000.0 / m_media.video.fps)
        : end - 1;
}

void EditorWindow::handlePlaybackEnd() {
    if (!m_player || m_loopSeeking || m_timelineActive || m_seekSettling) return;
    m_player->pause();
    m_loopSeeking = true;
    m_resumeAfterSeek = m_loopButton->isChecked();
    requestVideoSeek(m_resumeAfterSeek ? m_trimInMs : qMax(m_trimInMs, lastFrameMs()));
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
    // The footer's side columns follow their contents and the footer's width.
    if ((event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)
        && widget->objectName() == QLatin1String("ViewControls"))
        balanceFooter();
    if (event->type() == QEvent::ContextMenu) {
        // A button's menu opens on right-click: holding was the only way, and nobody guesses it.
        auto *button = qobject_cast<QToolButton *>(widget);
        if (button && button->isEnabled() && button->menu()) {
            m_tooltip->hide();
            button->showMenu();
            return true;
        }
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
    if (event->type() == QEvent::ToolTip && !widget->toolTip().isEmpty()) {
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

void EditorWindow::scheduleStripLoad() {
    if (!m_previewProvider || !isVisible()) return;
    m_previewProvider->requestStrip(m_timeline->thumbnailTimes(), QSize(128, 72) * devicePixelRatioF());
}

void EditorWindow::hideVideoPreview() {
    if (!m_videoPreview) return;
    m_hoverTime = -1;
    m_previewSampleTime = -1;
    m_previewTimer->stop();
    m_previewFade->setTarget(0);   // fades out on `fast` like it came in; hidden at 0
    m_previewProvider->cancelHover();
}

void EditorWindow::showVideoPreview() {
    if (m_hoverTime < 0 || !isVisible()) return;
    const bool appearing = m_videoPreview->isHidden();
    // The nearest decoded frame, when it differs, follows a step quieter.
    m_previewTime->setText(m_previewSampleTime >= 0 && m_previewSampleTime != m_hoverTime
        ? QStringLiteral("%1&nbsp;&nbsp;<span style=\"color:%2\">~%3</span>")
              .arg(formatPreciseTime(editedMs(m_hoverTime)), palette().color(QPalette::PlaceholderText).name(),
                   formatPreciseTime(editedMs(m_previewSampleTime)))
        : formatPreciseTime(editedMs(m_hoverTime)));
    m_videoPreview->adjustSize();
    QPoint pos = m_timeline->mapTo(this, m_hoverPoint);
    pos.setX(qBound(theme::kS1, pos.x() - m_videoPreview->width() / 2,
                    qMax(theme::kS1, width() - m_videoPreview->width() - theme::kS1)));
    pos.setY(qMax(theme::kS1, m_timeline->mapTo(this, QPoint()).y() - m_videoPreview->height() - theme::kS1));
    m_videoPreview->move(pos);
    m_videoPreview->show();
    m_videoPreview->raise();
    if (appearing) m_previewFade->jumpTo(0);
    m_previewFade->setTarget(1);   // also turns a fade-out around
    m_previewProvider->requestHover(m_hoverTime, QSize(256, 144) * devicePixelRatioF());
}

void EditorWindow::setVideoPreviewImage(const QImage &image) {
    QPixmap pixmap = QPixmap::fromImage(image).scaled(m_previewImage->size() * devicePixelRatioF(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_previewImage->setPixmap(pixmap);
}

namespace {
// A video frame as the document's background: upright, at its display size, in the
// canvas's format. Any thread.
QImage frameImage(const QVideoFrame &frame, QSize native) {
    QImage image = frame.toImage();
    if (image.isNull()) return image;
    // Qt < 6.8 includes rotation/mirroring in toImage(). Newer Qt
    // applies the surface transform there, but not the presentation one.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const int rotation = int(frame.rotation());
    if (rotation) image = image.transformed(QTransform().rotate(rotation));
    if (frame.mirrored()) image = image.transformed(QTransform().scale(-1, 1));
#endif
    if (image.size() != native)
        image = image.scaled(native, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
} // namespace

bool EditorWindow::updateVideoBackground() {
    if (m_videoBackgroundCurrent) return true;
    const QImage image = frameImage(m_lastVideoFrame, m_media.nativeSize());
    if (image.isNull()) return false;
    adoptVideoBackground(image);
    return true;
}

// Everything that reads the frame's pixels gets the new one.
void EditorWindow::adoptVideoBackground(const QImage &image) {
    m_bg = image;
    m_tools->setBackground(m_bg);
    m_ocr->setBackground(m_bg);
    for (QGraphicsItem *item : m_scene->items())
        if (auto *redact = dynamic_cast<RedactItem *>(item))
            redact->setSource(m_bg);
    m_videoBackgroundCurrent = true;
}

// Scrubbing while paused: the conversion runs on the pool like playing frames do, so a
// drag no longer stalls the editor; only the newest frame is fetched after the running job.
void EditorWindow::convertStillAsync(const QVideoFrame &frame) {
    if (m_stillConverting) { m_stillWaiting = frame; return; }
    m_stillConverting = true;
    QPointer<EditorWindow> self(this);
    const QSize native = m_media.nativeSize();
    QThreadPool::globalInstance()->start([self, frame, native] {
        const QImage image = frameImage(frame, native);
        QMetaObject::invokeMethod(qApp, [self, frame, image] {
            if (!self) return;
            self->m_stillConverting = false;
            const QVideoFrame waiting = std::exchange(self->m_stillWaiting, QVideoFrame());
            const bool playing = self->m_player && self->m_player->playbackState() == QMediaPlayer::PlayingState;
            if (!image.isNull() && !playing && frame == self->m_lastVideoFrame) {
                self->adoptVideoBackground(image);
                self->m_videoStill->setPixmap(QPixmap::fromImage(self->m_bg));
                self->m_videoStill->show();
            }
            if (waiting.isValid() && !playing) self->convertStillAsync(waiting);
        }, Qt::QueuedConnection);
    });
}

// At once, on the GUI thread: pausing must leave a picture even if the backend clears
// its surface right after. Frames arriving while paused go through convertStillAsync.
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
            // The seek in flight has shown a frame: a target waiting behind it goes now.
            if (m_seekInFlight) {
                m_seekInFlight = false;
                if ((m_timelineActive || m_keyStepping) && m_seekTimer->isActive()) flushVideoSeek();
            }
            m_videoBackgroundCurrent = false;
            // Pixels only where something reads them: a blur samples the frame, OCR reads it,
            // the Redact tool draws from it. A blacken cover needs none (700 ms of every 3 s
            // of 1080p playback went to converting frames for one).
            bool needsPixels = m_tools->tool() == ToolType::Redact;
            if (!needsPixels)
                for (QGraphicsItem *item : m_scene->items())
                    if (auto *redact = dynamic_cast<RedactItem *>(item);
                        redact && (RedactItem::isBlur(redact->mode()) || RedactItem::isOcr(redact->mode()))) {
                        needsPixels = true;
                        break;
                    }
            if (needsPixels) updateVideoBackground();
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_videoStill->hide();
            else if (m_videoBackgroundCurrent)
                showVideoStill();   // converted above for a tool that reads pixels
            else
                convertStillAsync(frame);
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
            if (m_miniMap && fadeTarget(m_miniMap) && updateVideoBackground())
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
        rebuildCamera();
        if (m_timeLabel) {
            m_timeLabel->setFixedWidth(m_timeLabel->fontMetrics().horizontalAdvance(widestTime()));
            m_timeLabel->setText(formatPreciseTime(editedMs(m_player->position())));
        }
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos){
        if (m_timelineActive || m_seekSettling) return;
        if (m_timeline && (m_presentedStart < 0 || m_player->playbackState() != QMediaPlayer::PlayingState))
            m_timeline->setPosition(pos);
        if (m_timeLabel)
            m_timeLabel->setText(formatPreciseTime(editedMs(pos)));
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
