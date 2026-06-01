#pragma once
#include <QWidget>
#include <QImage>
#include "config.h"
#include "cli.h"
class QGraphicsScene; class QUndoStack; class QResizeEvent; class QMouseEvent;
namespace eddy {
class Canvas; class Toolbar; class ToolController; class SelectionHandles;
class RedactBar; class Toast; class RedactOcrController; class RedactItem;
class DragPill;
enum class RedactMode;
class EditorWindow : public QWidget {
    Q_OBJECT
public:
    EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent=nullptr);
    QImage exportComposite();   // for tests + save/copy
public slots:
    void save();   // to file/save-dir per cli/config
    void copy();   // to clipboard
protected:
    void keyPressEvent(QKeyEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
private:
    void updateCompactMode();
    void refreshRedactBar();              // selection changed -> show/sync/position or hide
    void positionRedactBar();             // re-anchor over the selected redact
    void onRedactModeChosen(RedactMode m);
    RedactItem *selectedRedact() const;   // the sole selected RedactItem, or nullptr
    void doUndo();
    void doRedo();
    QImage m_bg; Config m_cfg; CliOptions m_cli; bool m_shown = false;
    bool m_compact = false;
    QGraphicsScene *m_scene; QUndoStack *m_undo;
    ToolController *m_tools; Canvas *m_canvas; Toolbar *m_toolbar;
    SelectionHandles *m_handles = nullptr;
    RedactOcrController *m_ocr = nullptr;
    RedactBar *m_redactBar = nullptr;
    Toast *m_toast = nullptr;
    DragPill *m_dragPill = nullptr;
};
}
