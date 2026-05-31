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
signals:
    void toolChosen(ToolType t);
    void colorChosen(const QColor &c);
    void saveRequested();
    void copyRequested();
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
};
}
