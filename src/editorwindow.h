#pragma once
#include <QWidget>
#include <QImage>
#include "config.h"
#include "cli.h"
#include "mediaio.h"
#include "exporter.h"
class QGraphicsScene; class QUndoStack; class QResizeEvent; class QMouseEvent;
class QGraphicsItem; class QGraphicsVideoItem; class QMediaPlayer;
class QToolButton; class QSlider; class QLabel;
class QTimer;
namespace eddy {
class Canvas; class Toolbar; class ToolController; class SelectionHandles;
class RedactBar; class Toast; class RedactOcrController; class RedactItem;
class DragPill;
enum class RedactMode;

bool imageSaveUsesShelfReturn(const CliOptions &cli);

class EditorWindow : public QWidget {
    Q_OBJECT
public:
    EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent=nullptr);
    EditorWindow(const MediaDocument &media, const Config &cfg, const CliOptions &cli, QWidget *parent=nullptr);
    QImage exportComposite();   // for tests + save/copy
public slots:
    void save();   // to file/save-dir per cli/config
    void copy();   // to clipboard
    void sendToShelf();
protected:
    void keyPressEvent(QKeyEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
private:
    bool isVideo() const { return m_media.kind == MediaKind::Video; }
    void updateCompactMode();
    void refreshRedactBar();              // selection changed -> show/sync/position or hide
    void positionRedactBar();             // re-anchor over the selected redact
    void onRedactModeChosen(RedactMode m);
    QWidget *createPlaybackBar();
    QImage renderAnnotationOverlay();
    bool hasVideoAnnotations() const;
    QString videoDeliveryPath();
    void onVideoContentChanged();
    void scheduleVideoExportCache(int delayMs = 350);
    void startVideoExportCache();
    void finishVideoExportCache(int revision, const QString &path, const DeliverResult &result);
    QString createVideoTempPath() const;
    DeliverResult exportVideoToFile(const QString &path);
    void copyVideoFile(const QString &path);
    bool postImageToShelf(const QImage &img, bool showSuccessToast);
    void saveVideo();
    void ensureVideoPlayer();
    void scheduleVideoLoad();
    RedactItem *selectedRedact() const;   // the sole selected RedactItem, or nullptr
    void doUndo();
    void doRedo();
    MediaDocument m_media;
    QImage m_bg; Config m_cfg; CliOptions m_cli; bool m_shown = false;
    bool m_compact = false;
    QGraphicsScene *m_scene; QUndoStack *m_undo;
    ToolController *m_tools; Canvas *m_canvas; Toolbar *m_toolbar;
    QGraphicsItem *m_backgroundItem = nullptr;
    QMediaPlayer *m_player = nullptr;
    QGraphicsVideoItem *m_videoItem = nullptr;
    QToolButton *m_playButton = nullptr;
    QSlider *m_positionSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    bool m_videoLoadQueued = false;
    QTimer *m_videoExportTimer = nullptr;
    QString m_cachedVideoPath;
    int m_videoRevision = 0;
    int m_cachedVideoRevision = -1;
    bool m_videoExportInProgress = false;
    bool m_videoExportPending = false;
    bool m_renderingVideoOverlay = false;
    int m_videoOverlayRenderGeneration = 0;
    SelectionHandles *m_handles = nullptr;
    RedactOcrController *m_ocr = nullptr;
    RedactBar *m_redactBar = nullptr;
    Toast *m_toast = nullptr;
    DragPill *m_dragPill = nullptr;
};
}
