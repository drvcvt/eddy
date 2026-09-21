#pragma once
#include <QWidget>
#include <QImage>
#include "config.h"
#include "cli.h"
#include "mediaio.h"
#include "exporter.h"
#include <QSet>
#include <QHash>
#include <QPointer>
#include <functional>
class QGraphicsScene; class QUndoStack; class QResizeEvent; class QMouseEvent; class QCloseEvent;
class QGraphicsItem; class QGraphicsVideoItem; class QMediaPlayer; class QAudioOutput;
class QToolButton; class QSlider; class QLabel; class QLineEdit;
class QTimer;
class QPropertyAnimation;
namespace eddy {
class Canvas; class Toolbar; class ToolController; class SelectionHandles;
class RedactBar; class Toast; class RedactOcrController; class RedactItem;
class TextBar; class TextItem;
class SpotlightBar; class SpotlightItem;
class DragPill;
class VideoTimeline;
class VideoPreviewProvider;
class CropController;
class CropBar;
enum class RedactMode;

enum class SaveRoute { ExplicitOutput, BoltsnapCard, ConfigDirectory, Shelf };
SaveRoute saveRoute(const CliOptions &cli, const Config &cfg);

class EditorWindow : public QWidget {
    Q_OBJECT
public:
    EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent=nullptr);
    EditorWindow(const MediaDocument &media, const Config &cfg, const CliOptions &cli, QWidget *parent=nullptr);
    ~EditorWindow() override;
    QImage exportComposite();   // for tests + save/copy
public slots:
    void save();   // to file/save-dir per cli/config
    void copy();   // to clipboard
    void copyVideoFrame();
    void sendToShelf();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
private:
    bool isVideo() const { return m_media.kind == MediaKind::Video; }
    void refreshRedactBar();              // selection changed -> show/sync/position or hide
    void positionRedactBar();             // re-anchor over the selected redact
    void refreshTextBar();
    void positionTextBar();
    TextItem *selectedText() const;
    void updateSelectedText(const std::function<void(TextItem *)> &change);
    SpotlightItem *selectedSpotlight() const;
    void refreshSpotlightBar();
    void positionSpotlightBar();
    void onRedactModeChosen(RedactMode m);
    QWidget *createPlaybackBar();
    QImage renderAnnotationOverlay();
    bool hasVideoAnnotations() const;
    bool hasVideoEdits() const;
    bool hasTrim() const;
    void applyTrimRange(qint64 inMs, qint64 outMs);
    void setTrimRangeState(qint64 inMs, qint64 outMs);
    void updateTrimTimeLabels(qint64 inMs, qint64 outMs);
    void commitTrimTime(QLineEdit *field);
    void requestVideoSeek(qint64 position);
    void flushVideoSeek();
    void finishVideoSeek();
    QString videoDeliveryPath();
    void onVideoContentChanged();
    void scheduleVideoExportCache(int delayMs = 350);
    void startVideoExportCache();
    void finishVideoExportCache(int revision, const QString &path, const DeliverResult &result);
    QString createVideoTempPath() const;
    void completePendingVideoActions(const QString &path, bool takeOwnership);
    void failPendingVideoActions();
    void runVideoIpc(const std::function<DeliverResult()> &operation,
                     const std::function<void(const DeliverResult &)> &completion,
                     const QString &pinnedPath = {});
    void replaceVideoCard(const QString &path, bool copyAfter = false);
    void startVideoFileSave(const QString &source, const QString &destination,
                            bool copyAfter, bool closeAfter);
    void finishVideoFileSave(const QString &path, const DeliverResult &result);
    void copyVideoFile(const QString &path);
    bool postImageToShelf(const QImage &img, bool showSuccessToast);
    void postVideoToShelf(const QString &path, bool takeOwnership, bool copyAfter = false,
                          bool fallbackOnFailure = false);
    void saveVideo();
    void ensureVideoPlayer();
    void togglePlayback();
    void handlePlaybackEnd();
    void scheduleVideoLoad();
    void scheduleContactSheetLoad();
    void hideVideoPreview();
    void showVideoPreview();
    void setVideoPreviewImage(const QImage &image);
    RedactItem *selectedRedact() const;   // the sole selected RedactItem, or nullptr
    void doUndo();
    void doRedo();
    void toggleTheme();
    void setupCrop();
    void finishCrop();
    void setCropRect(QRect rect);
    void positionCropBar();
    CropController *m_crop = nullptr;
    CropBar *m_cropBar = nullptr;
    QRect m_cropRect;
    QTransform m_beforeCropView;
    QPointF m_beforeCropCenter;
    bool m_beforeCropFit = false;
    QList<QGraphicsItem *> m_beforeCropSelection;
    MediaDocument m_media;
    QImage m_bg; Config m_cfg; CliOptions m_cli; bool m_shown = false;
    QGraphicsScene *m_scene; QUndoStack *m_undo;
    ToolController *m_tools; Canvas *m_canvas; Toolbar *m_toolbar;
    QGraphicsItem *m_backgroundItem = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QGraphicsVideoItem *m_videoItem = nullptr;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_muteButton = nullptr;
    QToolButton *m_loopButton = nullptr;
    QToolButton *m_speedButton = nullptr;
    bool m_loopSeeking = false;
    VideoTimeline *m_timeline = nullptr;
    VideoPreviewProvider *m_previewProvider = nullptr;
    QWidget *m_videoPreview = nullptr;
    QLabel *m_previewImage = nullptr;
    QLabel *m_previewTime = nullptr;
    QTimer *m_previewTimer = nullptr;
    QTimer *m_stripTimer = nullptr;
    QPropertyAnimation *m_previewFade = nullptr;
    qint64 m_hoverTime = -1;
    qint64 m_previewSampleTime = -1;
    QPoint m_hoverPoint;
    QSlider *m_volumeSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_exportStatus = nullptr;
    QLineEdit *m_trimInLabel = nullptr;
    QLineEdit *m_trimOutLabel = nullptr;
    QLabel *m_trimDurationLabel = nullptr;
    QTimer *m_seekTimer = nullptr;
    QTimer *m_seekSettleTimer = nullptr;
    qint64 m_seekTarget = -1;
    qint64 m_presentedStart = -1, m_presentedEnd = -1;
    qint64 m_frameTimeOrigin = 0;
    bool m_timelineActive = false;
    bool m_seekSettling = false;
    bool m_resumeAfterSeek = false;
    bool m_hasVideoFrame = false;
    bool m_hasSentVideoSeek = false;
    bool m_copyFramePending = false;
    QLabel *m_tooltip = nullptr;
    QTimer *m_tooltipTimer = nullptr;
    QPointer<QWidget> m_tooltipOwner;
    bool m_spaceArmed = false;
    bool m_spaceConsumed = false;
    qint64 m_trimInMs = 0;
    qint64 m_trimOutMs = 0;
    bool m_videoLoadQueued = false;
    QTimer *m_videoExportTimer = nullptr;
    QString m_cachedVideoPath;
    QSet<QString> m_clipboardVideoPaths;
    QHash<QString, int> m_videoIpcPaths;
    int m_videoRevision = 0;
    int m_cachedVideoRevision = -1;
    bool m_videoExportInProgress = false;
    bool m_videoExportPending = false;
    bool m_videoStatusRequested = false;
    bool m_copyVideoPending = false;
    bool m_sendVideoToShelfPending = false;
    bool m_videoShelfFallbackPending = false;
    bool m_replaceVideoCardPending = false;
    QString m_videoSavePendingPath;
    bool m_videoSavePendingCopy = false;
    bool m_videoSavePendingClose = false;
    bool m_videoSaveInProgress = false;
    QString m_videoSaveSourcePath;
    bool m_copyAfterVideoSave = false;
    bool m_closeAfterVideoSave = false;
    bool m_closeAfterVideoShelf = false;
    bool m_closeAfterVideoCard = false;
    int m_videoIpcInProgress = 0;
    bool m_closeAfterVideoIpc = false;
    bool m_closeAfterVideoExport = false;
    bool m_renderingVideoOverlay = false;
    int m_videoOverlayRenderGeneration = 0;
    SelectionHandles *m_handles = nullptr;
    RedactOcrController *m_ocr = nullptr;
    RedactBar *m_redactBar = nullptr;
    TextBar *m_textBar = nullptr;
    SpotlightBar *m_spotlightBar = nullptr;
    Toast *m_toast = nullptr;
    DragPill *m_dragPill = nullptr;
    bool m_dark = false;
};
}
