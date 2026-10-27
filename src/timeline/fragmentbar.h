#pragma once
#include <QWidget>
#include "studio/studiodocument.h"
class QToolButton;
namespace eddy {

// The chosen piece's floating bar (studio plan 6.6): its speed, and join with the piece
// before it. Deleting is the transport row's Delete (K).
class FragmentBar : public QWidget {
    Q_OBJECT
public:
    explicit FragmentBar(QWidget *parent = nullptr);
    void setFragment(const Fragment &fragment, bool canJoin);
    void refreshTheme();
signals:
    void speedChosen(double speed);
    void joinRequested();
private:
    QToolButton *m_speed = nullptr;
    QToolButton *m_join = nullptr;
};

}
