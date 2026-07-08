#pragma once
#include <QWidget>
#include <QHash>
#include "toolcontroller.h"
class QToolButton; class QPropertyAnimation; class QShowEvent; class QResizeEvent;
namespace eddy {
class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget *parent=nullptr);
    void setAnimationsEnabled(bool on) { m_anim = on; }
public slots:
    void syncTool(ToolType t);            // reflect external (keyboard) tool change
    void setUndoEnabled(bool on);
    void setRedoEnabled(bool on);
    void setSwatchColor(const QColor &c); // tint the colour-swatch dot to the current stroke colour
signals:
    void toolChosen(ToolType t);
    void colorChosen(const QColor &c);
    void saveRequested();
    void copyRequested();
    void sendToShelfRequested();
    void widthChosen(double w);
    void undoRequested();
    void redoRequested();
    void eyedropperRequested();   // user chose the pipette in the colour popover
protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
private:
    void movePillTo(QToolButton *b, bool animate);
    QHash<int, QToolButton*> m_btns;      // keyed by int(ToolType)
    QWidget *m_pill = nullptr;
    QPropertyAnimation *m_pillAnim = nullptr;
    QToolButton *m_active = nullptr;
    bool m_anim = true;
    QToolButton *m_undoBtn = nullptr;
    QToolButton *m_redoBtn = nullptr;
    QToolButton *m_swatch = nullptr;
};
}
