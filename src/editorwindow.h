#pragma once
#include <QWidget>
#include <QImage>
#include "config.h"
#include "cli.h"
#include "mediaio.h"
#include "exporter.h"
#include <QSet>
#include <QHash>
#include <atomic>
#include <functional>
#include <memory>
class QGraphicsScene; class QUndoStack; class QResizeEvent; class QMouseEvent; class QCloseEvent;
class QGraphicsItem; class QGraphicsVideoItem; class QMediaPlayer; class QAudioOutput;
class QToolButton; class QSlider; class QLabel;
class QTimer;
namespace eddy {
class Canvas; class Toolbar; class ToolController; class SelectionHandles;
class RedactBar; class Toast; class RedactOcrController; class RedactItem;
class TextBar; class TextItem;
class SpotlightBar; class SpotlightItem;
class DragPill;
class VideoTimeline;
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
    void sendToShelf();
protected:
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
private:
    bool isVideo() const { return m_media.kind == MediaKind::Video; }
    void updateCompactMode();
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
    void scheduleVideoLoad();
    void scheduleContactSheetLoad();
    RedactItem *selectedRedact() const;   // the sole selected RedactItem, or nullptr
    void doUndo();
    void doRedo();
    void toggleTheme();
    MediaDocument m_media;
    QImage m_bg; Config m_cfg; CliOptions m_cli; bool m_shown = false;
    bool m_compact = false;
    QGraphicsScene *m_scene; QUndoStack *m_undo;
    ToolController *m_tools; Canvas *m_canvas; Toolbar *m_toolbar;
    QGraphicsItem *m_backgroundItem = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QGraphicsVideoItem *m_videoItem = nullptr;
    QToolButton *m_playButton = nullptr;
    QToolButton *m_muteButton = nullptr;
    VideoTimeline *m_timeline = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_trimInLabel = nullptr;
    QLabel *m_trimOutLabel = nullptr;
    qint64 m_trimInMs = 0;
    qint64 m_trimOutMs = 0;
    bool m_videoLoadQueued = false;
    bool m_contactSheetQueued = false;
    QTimer *m_videoExportTimer = nullptr;
    QString m_cachedVideoPath;
    QSet<QString> m_clipboardVideoPaths;
    QHash<QString, int> m_videoIpcPaths;
    int m_videoRevision = 0;
    int m_cachedVideoRevision = -1;
    bool m_videoExportInProgress = false;
    bool m_videoExportPending = false;
    std::shared_ptr<std::atomic_bool> m_videoExportCancelRequested;
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
