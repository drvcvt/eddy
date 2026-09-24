#pragma once
#include <QWidget>
#include <QHash>
#include "toolcontroller.h"
class QToolButton;
class QMenu;
namespace eddy {
class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget *parent=nullptr);
    QWidget *toolRail() const { return m_toolRail; }
    void enableVideoFrameCopy();
    // Holding Save (or Alt+Down on it) opens `panel` as its popover.
    QMenu *enableExportMenu(QWidget *panel);
public slots:
    void syncTool(ToolType t);            // reflect external (keyboard) tool change
    void setUndoEnabled(bool on);
    void setRedoEnabled(bool on);
    void setSwatchColor(const QColor &c); // tint the colour-swatch dot to the current stroke colour
    void setDark(bool dark);
    void setStudioActive(bool on);        // Studio framing is on for this document
signals:
    void toolChosen(ToolType t);
    void colorChosen(const QColor &c);
    void saveRequested();
    void copyRequested();
    void copyFrameRequested();
    void sendToShelfRequested();
    void widthChosen(double w);
    void undoRequested();
    void redoRequested();
    void eyedropperRequested();   // user chose the pipette in the colour popover
    void themeToggleRequested();
    void studioRequested();
private:
    QWidget *m_toolRail = nullptr;
    QHash<int, QToolButton*> m_btns;      // keyed by int(ToolType)
    QToolButton *m_undoBtn = nullptr;
    QToolButton *m_redoBtn = nullptr;
    QToolButton *m_swatch = nullptr;
    QToolButton *m_themeBtn = nullptr;
    QToolButton *m_studioBtn = nullptr;
    QColor m_swatchColor;
};
}
