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

QWidget *EditorWindow::createPlaybackBar() {
    auto *bar = new PlaybackBar(this);
    bar->setObjectName("PlaybackBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QVBoxLayout(bar);
    // One left and right edge with the tool rail and footer (kBarPad from the window).
    // The transport row sits kS2 under the strip it drives and kS3 above the footer,
    // whose own kBarPad completes that gap: it reads as the timeline's, not the footer's.
    lay->setContentsMargins(theme::kBarPad, theme::kS2, theme::kBarPad, theme::kS3 - theme::kBarPad);
    lay->setSpacing(theme::kS2);
    auto *playback = new QGridLayout;
    playback->setContentsMargins(0, 0, 0, 0);
    playback->setHorizontalSpacing(theme::kS2);
    playback->setColumnStretch(0, 1);
    playback->setColumnStretch(2, 1);
    auto *leftGroup = new QHBoxLayout;
    leftGroup->setSpacing(theme::kS2);
    auto *rightGroup = new QHBoxLayout;
    rightGroup->setSpacing(theme::kS2);
    auto *trimControls = new TrimField(bar);
    trimControls->setObjectName(QStringLiteral("TrimControls"));
    auto *trim = new QHBoxLayout(trimControls);
    trim->setContentsMargins(theme::kS1, TrimField::kInset, theme::kS1, TrimField::kInset);
    trim->setSpacing(0);   // captions and values bring their own kS2 padding

    m_playButton = new QToolButton(bar);
    const QColor iconColor = palette().color(QPalette::ButtonText);
    m_playButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/play.svg"), iconColor, iconColor));
    m_playButton->setObjectName("PlaybackPlay");
    m_playButton->setAutoRaise(true);
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setFixedSize(theme::kControl);
    m_playButton->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_playButton->setToolTip(QStringLiteral("Play / Pause\tSpace / K"));
    m_playButton->setAccessibleName(theme::spokenName(m_playButton->toolTip()));
    // The position only, to the millisecond like the trim fields; the length is End and
    // the ruler's. Wide enough for the last moment, so it never changes width.
    m_timeLabel = new QLabel(formatPreciseTime(0), bar);
    m_timeLabel->setObjectName("PlaybackTime");
    m_timeLabel->ensurePolished();
    m_timeLabel->setFixedWidth(m_timeLabel->fontMetrics().horizontalAdvance(
        formatPreciseTime(m_media.video.durationMs)));

    m_muteButton = new QToolButton(bar);
    m_muteButton->setObjectName("PlaybackMute");
    m_muteButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/volume.svg"), iconColor, iconColor));
    m_muteButton->setAutoRaise(true);
    m_muteButton->setFocusPolicy(Qt::NoFocus);
    m_muteButton->setCursor(Qt::PointingHandCursor);
    m_muteButton->setFixedSize(theme::kControl);
    m_muteButton->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_muteButton->setToolTip(tr("Mute audio\nHold for volume"));
    m_muteButton->setAccessibleName(theme::spokenName(m_muteButton->toolTip()));

    // Volume lives in the speaker's hold menu only: one control for it, and a row
    // that shows the same controls at every width.
    auto *audioMenu = new QMenu(m_muteButton);
    auto *audioWidget = new QWidget(audioMenu);
    auto *audioLayout = new QHBoxLayout(audioWidget);
    audioLayout->setContentsMargins(theme::kS2, theme::kS2, theme::kS2, theme::kS2);
    m_volumeSlider = new QSlider(Qt::Horizontal, audioWidget);
    m_volumeSlider->setObjectName(QStringLiteral("PlaybackVolume"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(96);
    m_volumeSlider->setAccessibleName(tr("Volume"));
    audioLayout->addWidget(m_volumeSlider);
    auto *audioAction = new QWidgetAction(audioMenu);
    audioAction->setDefaultWidget(audioWidget);
    audioMenu->addAction(audioAction);
    // Muting is the preview's; this one decides what the output carries.
    m_outputAudio = audioMenu->addAction(tr("Include audio in output"));
    m_outputAudio->setCheckable(true);
    m_outputAudio->setChecked(true);
    m_outputAudio->setEnabled(m_media.video.hasAudio);
    m_outputAudio->setToolTip(tr("Off, the saved video has no sound; the preview still plays it"));
    connect(m_outputAudio, &QAction::toggled, this, [this](bool on) {
        editStudio([on](StudioDocument &d) { d.audio = on; });
    });
    m_noAudio = new QLabel(tr("No audio"), bar);
    m_noAudio->setObjectName(QStringLiteral("NoAudio"));
    m_noAudio->setToolTip(tr("The output has no sound; turn it back on in the speaker menu"));
    m_noAudio->hide();
    m_muteButton->setMenu(audioMenu);
    m_muteButton->setPopupMode(QToolButton::DelayedPopup);
    m_muteButton->setFocusPolicy(Qt::StrongFocus);

    leftGroup->addWidget(m_playButton);
    leftGroup->addWidget(m_timeLabel);
    playback->addLayout(leftGroup, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_timeline = new VideoTimeline(bar);
    m_timeline->setDuration(m_media.video.durationMs);
    m_timeline->setMinimumRange(m_media.video.fps > 0.0
        ? qMax<qint64>(1, qRound64(1000.0 / m_media.video.fps)) : 1);
    m_timeline->setTrimRange(m_trimInMs, m_trimOutMs);
    if (m_media.video.hasAudio && !m_media.path.isEmpty())
        m_timeline->setWaveform(new AudioWaveformProvider(m_media.path, m_media.video.durationMs,
                                                          m_media.video.audioOffsetMs, m_timeline));
    m_trimInLabel = new QLineEdit(formatPreciseTime(m_trimInMs), bar);
    m_trimInLabel->setObjectName(QStringLiteral("TrimInTime"));
    m_trimOutLabel = new QLineEdit(formatPreciseTime(m_trimOutMs), bar);
    m_trimOutLabel->setObjectName(QStringLiteral("TrimOutTime"));
    m_trimDurationLabel = new QLabel(bar);
    m_trimDurationLabel->setObjectName(QStringLiteral("TrimDuration"));
    m_trimDurationLabel->setToolTip(QStringLiteral("Selected duration"));
    for (auto *field : {m_trimInLabel, m_trimOutLabel}) {
        field->ensurePolished();
        field->setMaxLength(20);
        field->setFixedHeight(theme::kCtl);
        field->setAccessibleName(field == m_trimInLabel ? tr("Trim start") : tr("Trim end"));
        field->setToolTip(field == m_trimInLabel
            ? tr("Start of the exported clip\nApply\tEnter\nCancel\tEsc")
            : tr("End of the exported clip\nApply\tEnter\nCancel\tEsc"));
        connect(field, &QLineEdit::editingFinished, this, [this, field] { commitTrimTime(field); });
    }
    updateTrimTimeLabels(m_trimInMs, m_trimOutMs);
    auto makeTrimButton = [bar](const QString &text, const QString &name) {
        auto *button = new theme::CapButton(bar);
        button->setText(text);
        button->setObjectName(name);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(theme::kCtl);
        return button;
    };
    auto *setIn = makeTrimButton(tr("Start"), QStringLiteral("TrimSetIn"));
    auto *setOut = makeTrimButton(tr("End"), QStringLiteral("TrimSetOut"));
    auto *reset = makeTrimButton({}, QStringLiteral("TrimReset"));
    reset->setIcon(theme::tintedIcon(QStringLiteral(":/icons/reset.svg"), iconColor, iconColor));
    reset->setFixedWidth(theme::kCtl);
    reset->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    setIn->setToolTip(tr("Set start to playhead\tI"));
    setOut->setToolTip(tr("Set end to playhead\tO"));
    setIn->setAccessibleName(theme::spokenName(setIn->toolTip()));
    setOut->setAccessibleName(theme::spokenName(setOut->toolTip()));
    reset->setToolTip(tr("Reset trim to the complete clip"));
    reset->setAccessibleName(theme::spokenName(reset->toolTip()));

    trim->addWidget(setIn);
    trim->addWidget(m_trimInLabel);
    trim->addSpacing(theme::kS3);
    trim->addWidget(setOut);
    trim->addWidget(m_trimOutLabel);
    trim->addSpacing(theme::kS3);
    // Duration reads like the pairs before it: a caption boxed like Start and End,
    // its value boxed like their time fields (updateTrimTimeLabels sets its width).
    auto *durationCaption = new QLabel(tr("Duration"), trimControls);
    durationCaption->setObjectName(QStringLiteral("TrimDurationCaption"));
    durationCaption->ensurePolished();
    durationCaption->setContentsMargins(theme::kS2, 0, theme::kS2, 0);
    durationCaption->setFixedWidth(durationCaption->fontMetrics().horizontalAdvance(durationCaption->text())
                                   + 2 * theme::kS2);
    m_trimDurationLabel->ensurePolished();
    m_trimDurationLabel->setContentsMargins(theme::kS2 + 2, 0, theme::kS2 + 2, 0);
    trim->addWidget(durationCaption);
    trim->addWidget(m_trimDurationLabel);
    // Reset's ink sits (kCtl - ink) / 2 in, a caption's kS2: the pairs' kS3 makes up the rest.
    trim->addSpacing(theme::kS3 + theme::kS2 - (theme::kCtl - theme::iconInk(theme::kIconSize)) / 2);
    trim->addWidget(reset);
    playback->addWidget(trimControls, 0, 1, Qt::AlignCenter);
    m_loopButton = new QToolButton(bar);
    m_loopButton->setObjectName(QStringLiteral("PlaybackLoop"));
    m_loopButton->setCheckable(true);
    m_loopButton->setAutoRaise(true);
    m_loopButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/loop.svg"), iconColor, iconColor));
    m_loopButton->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_loopButton->setFixedSize(theme::kControl);
    m_loopButton->setToolTip(tr("Loop the selected range"));
    m_loopButton->setAccessibleName(theme::spokenName(m_loopButton->toolTip()));
    m_speedButton = new QToolButton(bar);
    m_speedButton->setObjectName(QStringLiteral("PlaybackSpeed"));
    m_speedButton->setText(QStringLiteral("1×"));
    m_speedButton->setFixedHeight(theme::kCtl);
    m_speedButton->setToolTip(tr("Preview speed"));
    m_speedButton->setAccessibleName(theme::spokenName(m_speedButton->toolTip()));
    m_speedButton->setPopupMode(QToolButton::InstantPopup);
    auto *rates = new QMenu(m_speedButton);
    rates->setObjectName(QStringLiteral("PlaybackRateMenu"));
    rates->setWindowFlag(Qt::FramelessWindowHint);
    rates->setAttribute(Qt::WA_TranslucentBackground);
    auto *rateGroup = new QActionGroup(rates);
    for (qreal rate : {0.25, 0.5, 1.0, 1.5, 2.0}) {
        auto *action = rates->addAction(QStringLiteral("%1×").arg(rate));
        action->setCheckable(true);
        action->setChecked(rate == 1.0);
        action->setData(rate);
        rateGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, rate] {
            ensureVideoPlayer();
            m_previewRate = rate;
            theme::setMenuLabel(m_speedButton, QStringLiteral("%1×").arg(rate), kWidestRate);
            for (auto *other : m_speedButton->menu()->actions())
                other->setChecked(qFuzzyCompare(other->data().toDouble(), rate));
            if (m_player) m_player->setPlaybackRate(rate);
            // Paused, a new rate is only remembered: following the pieces would seek out of
            // a cut, or end playback in a trailing one and start the loop.
            if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState)
                followPlaybackPieces(m_player->position());
        });
    }
    m_speedButton->setMenu(rates);
    theme::setMenuArrow(m_speedButton);
    theme::setMenuLabel(m_speedButton, m_speedButton->text(), kWidestRate);
    rightGroup->addWidget(m_loopButton);
    rightGroup->addWidget(m_speedButton);
    rightGroup->addWidget(m_noAudio);
    rightGroup->addWidget(m_muteButton);
    playback->addLayout(rightGroup, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    lay->addWidget(m_timeline);
    lay->addLayout(playback);

    m_previewProvider = new VideoPreviewProvider(m_media.path, this);
    m_videoPreview = new QWidget(this);
    m_videoPreview->setObjectName(QStringLiteral("VideoPreview"));
    m_videoPreview->setAttribute(Qt::WA_StyledBackground);
    m_videoPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *previewLayout = new QVBoxLayout(m_videoPreview);
    previewLayout->setContentsMargins(theme::kS1, theme::kS1, theme::kS1, theme::kS1);
    previewLayout->setSpacing(theme::kS1);
    m_previewImage = new QLabel(m_videoPreview);
    m_previewImage->setFixedSize(192, 108);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewTime = new QLabel(m_videoPreview);
    m_previewTime->setTextFormat(Qt::RichText);
    m_previewTime->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewImage);
    previewLayout->addWidget(m_previewTime);
    m_videoPreview->hide();
    auto *previewOpacity = new QGraphicsOpacityEffect(m_videoPreview);
    previewOpacity->setOpacity(1);
    m_videoPreview->setGraphicsEffect(previewOpacity);
    m_previewFade = new Spring(motion::kFast, [previewOpacity](qreal opacity) {
        previewOpacity->setOpacity(opacity);
    }, m_videoPreview);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(150);
    connect(m_previewTimer, &QTimer::timeout, this, &EditorWindow::showVideoPreview);
    m_stripTimer = new QTimer(this);
    m_stripTimer->setSingleShot(true);
    m_stripTimer->setInterval(100);
    connect(m_stripTimer, &QTimer::timeout, this, &EditorWindow::scheduleStripLoad);
    connect(m_timeline, &VideoTimeline::viewRangeChanged, this, [this] {
        hideVideoPreview();
        m_stripTimer->start();
    });
    connect(m_timeline, &VideoTimeline::hoverRequested, this, [this](qint64 time, QPoint point) {
        if (time != m_hoverTime) {
            qint64 sampled = -1;
            const QImage nearby = m_timeline->thumbnailNear(time, &sampled);
            if (!nearby.isNull()) {
                setVideoPreviewImage(nearby);
                m_previewSampleTime = sampled;
            } else {
                m_previewImage->setText(QStringLiteral("…"));
                m_previewSampleTime = -1;
            }
        }
        m_hoverTime = time;
        m_hoverPoint = point;
        if (m_videoPreview->isVisible() || m_timeline->trimming()) showVideoPreview();
        else if (!m_previewTimer->isActive()) m_previewTimer->start();
    });
    connect(m_timeline, &VideoTimeline::hoverLeft, this, &EditorWindow::hideVideoPreview);
    connect(m_previewProvider, &VideoPreviewProvider::thumbnailReady,
            m_timeline, &VideoTimeline::setThumbnail);
    connect(m_previewProvider, &VideoPreviewProvider::hoverReady, this,
            [this](qint64 time, const QImage &image) {
        if (time != m_hoverTime || !m_videoPreview->isVisible()) return;
        setVideoPreviewImage(image);
        m_previewSampleTime = time;
        m_previewTime->setText(formatPreciseTime(time));
    });
    connect(m_previewProvider, &VideoPreviewProvider::hoverFailed, this, [this](qint64 time) {
        if (time == m_hoverTime && m_previewSampleTime < 0) m_previewImage->setText(tr("Preview unavailable"));
    });

    connect(m_playButton, &QToolButton::clicked, this, &EditorWindow::togglePlayback);
    connect(m_muteButton, &QToolButton::clicked, this, [this]{
        ensureVideoPlayer();
        if (!m_audioOutput) return;
        const bool muted = !m_audioOutput->isMuted();
        m_audioOutput->setMuted(muted);
        m_muteButton->setIcon(theme::tintedIcon(
            muted ? QStringLiteral(":/icons/muted.svg") : QStringLiteral(":/icons/volume.svg"),
            palette().color(QPalette::ButtonText), palette().color(QPalette::ButtonText)));
        m_muteButton->setToolTip(muted ? tr("Unmute audio\nHold for volume") : tr("Mute audio\nHold for volume"));
        m_muteButton->setAccessibleName(theme::spokenName(m_muteButton->toolTip()));
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value){
        ensureVideoPlayer();
        if (!m_audioOutput) return;
        m_audioOutput->setVolume(value / 100.0f);
        if (value > 0 && m_audioOutput->isMuted()) {
            m_audioOutput->setMuted(false);
            const QColor color = palette().color(QPalette::ButtonText);
            m_muteButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/volume.svg"), color, color));
            m_muteButton->setToolTip(tr("Mute audio\nHold for volume"));
            m_muteButton->setAccessibleName(theme::spokenName(m_muteButton->toolTip()));
        }
    });
    m_seekTimer = new QTimer(this);
    m_seekTimer->setSingleShot(true);
    m_seekTimer->setInterval(33);
    connect(m_seekTimer, &QTimer::timeout, this, &EditorWindow::flushVideoSeek);
    m_seekSettleTimer = new QTimer(this);
    m_seekSettleTimer->setSingleShot(true);
    m_seekSettleTimer->setInterval(2000);
    connect(m_seekSettleTimer, &QTimer::timeout, this, [this] {
        m_resumeAfterSeek = false;
        m_seekSettling = false;
        m_loopSeeking = false;
        m_seekTarget = -1;
        if (m_copyFramePending) {
            m_copyFramePending = false;
            if (m_toast) m_toast->showMessage(tr("Frame unavailable; try again after seeking"));
        }
    });
    connect(m_timeline, &VideoTimeline::interactionStarted, this, [this](bool trimming) {
        m_timelineTrimming = trimming;
        ensureVideoPlayer();
        m_timelineActive = true;
        m_resumeAfterSeek = !trimming && m_player
            && m_player->playbackState() == QMediaPlayer::PlayingState;
        if (m_player) m_player->pause();
        if (m_videoExportTimer) m_videoExportTimer->stop();
    });
    connect(m_timeline, &VideoTimeline::seekRequested, this, &EditorWindow::requestVideoSeek);
    connect(m_timeline, &VideoTimeline::interactionFinished, this, [this](bool cancelled) {
        m_timelineActive = false;
        // A click on the film strip selects the fragment under the playhead.
        if (!cancelled && !m_timelineTrimming && m_studio.fragments.size() > 1)
            selectFragment(fragments::indexAt(m_studio.fragments, m_timeline->position()));
        if (cancelled) m_resumeAfterSeek = m_copyFramePending = false;
        flushVideoSeek();
        if (hasVideoEdits() && m_cachedVideoRevision != m_videoRevision) scheduleVideoExportCache();
    });
    connect(m_timeline, &VideoTimeline::trimCommitted,
            this, &EditorWindow::applyTrimRange);
    connect(m_timeline, &VideoTimeline::trimPreviewed,
            this, &EditorWindow::updateTrimTimeLabels);
    connect(m_timeline, &VideoTimeline::zoomAddRequested, this, &EditorWindow::addZoomAt);
    connect(m_timeline, &VideoTimeline::zoomSelected, this, &EditorWindow::selectZoom);
    connect(m_timeline, &VideoTimeline::cutClicked, this, &EditorWindow::selectFragment);
    auto itemFor = [this](quintptr key) -> AnnotationItem * {
        for (QGraphicsItem *item : m_scene->items())
            if (quintptr(item) == key) return dynamic_cast<AnnotationItem *>(item);
        return nullptr;
    };
    // The mask lane selects an item, going to its window when it is not showing.
    connect(m_timeline, &VideoTimeline::maskSelected, this, [this, itemFor](quintptr key) {
        AnnotationItem *item = itemFor(key);
        if (!item || !item->timeWindow()) return;
        const auto window = *item->timeWindow();
        if (m_timeline->position() < window.first || m_timeline->position() >= window.second) {
            if (m_player) m_player->pause();
            m_timeline->setPosition(window.first);
            requestVideoSeek(window.first);
            flushVideoSeek();
        }
        applyTimeWindows(window.first > m_timeline->position() ? window.first : m_timeline->position());
        m_scene->clearSelection();
        item->setVisible(true);
        item->setSelected(true);
    });
    connect(m_timeline, &VideoTimeline::maskWindowPreviewed, this,
            [this, itemFor](quintptr key, qint64 from, qint64 to) {
        if (AnnotationItem *item = itemFor(key)) item->setTimeWindow(std::pair{from, to});
    });
    connect(m_timeline, &VideoTimeline::maskWindowEdited, this,
            [this, itemFor](quintptr key, qint64 from, qint64 to, qint64 beforeFrom, qint64 beforeTo) {
        AnnotationItem *item = itemFor(key);
        if (!item) return;
        m_undo->push(new SetTimeWindowCommand(item, std::pair{beforeFrom, beforeTo}, std::pair{from, to}, [this] {
            refreshMasks();
            applyTimeWindows(m_timeline->position());
            onVideoContentChanged();
        }));
    });
    connect(m_timeline, &VideoTimeline::zoomsPreviewed, this, [this](const QVector<ZoomSegment> &zooms) {
        m_studio.zooms = zooms;   // the drag's own preview; the edit is one undo step at release
        if (!m_cameraRebuild->isActive()) m_cameraRebuild->start();
    });
    connect(m_timeline, &VideoTimeline::zoomsEdited, this,
            [this](const QVector<ZoomSegment> &before, const QVector<ZoomSegment> &after) {
        StudioDocument from = m_studio, to = m_studio;
        from.zooms = before;
        to.zooms = after;
        m_undo->push(new SetStudioDocumentCommand(from, to,
            [this](const StudioDocument &d) { setStudioDocument(d); }));
    });
    connect(m_timeline, &VideoTimeline::zoomMenuRequested, this, [this](quint32 id, QPoint global) {
        selectZoom(id);
        QMenu menu(this);
        const QColor ink = palette().color(QPalette::WindowText);
        for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                            ZoomSegment::Motion::Instant}) {
            QAction *action = menu.addAction(motionIcon(motion, ink, theme::kFsUi), motionName(motion));
            connect(action, &QAction::triggered, this, [this, id, motion] {
                editZoom(id, [motion](ZoomSegment &z) { z.motion = motion; });
            });
        }
        menu.addSeparator();
        connect(menu.addAction(tr("Remove zoom\tDelete")), &QAction::triggered, this,
                [this, id] { removeZoom(id); });
        menu.exec(global);
    });
    connect(setIn, &QToolButton::clicked, this, &EditorWindow::setTrimStartAtPlayhead);
    connect(setOut, &QToolButton::clicked, this, &EditorWindow::setTrimEndAtPlayhead);
    connect(reset, &QToolButton::clicked, this, [this]{
        m_timeline->setTrimRange(0, m_timeline->duration());
        applyTrimRange(m_timeline->trimIn(), m_timeline->trimOut());
    });
    bar->stack = lay; bar->transport = playback; bar->left = leftGroup; bar->right = rightGroup;
    bar->trim = trimControls;
    bar->adapt();
    return bar;
}

} // namespace eddy
