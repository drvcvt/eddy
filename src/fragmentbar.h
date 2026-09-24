#pragma once
#include <QWidget>
#include "studiodocument.h"
class QToolButton;
namespace eddy {

// The selected fragment's floating bar (studio plan 6.6): speed, cut or
// restore, and join with the fragment before it.
class FragmentBar : public QWidget {
    Q_OBJECT
public:
    explicit FragmentBar(QWidget *parent = nullptr);
    void setFragment(const Fragment &fragment, bool canJoin);
    void refreshTheme();
signals:
    void speedChosen(double speed);
    void cutToggled();
    void joinRequested();
private:
    QToolButton *m_speed = nullptr;
    QToolButton *m_cut = nullptr;
    QToolButton *m_join = nullptr;
};

}
