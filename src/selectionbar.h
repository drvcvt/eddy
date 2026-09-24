#pragma once
#include <QWidget>
#include "snapping.h"
class QToolButton;

namespace eddy {
// Context bar for two or more selected items (21.09. plan 6): line them up
// on the common bounds, and from three on share the gaps evenly.
class SelectionBar : public QWidget {
    Q_OBJECT
public:
    explicit SelectionBar(QWidget *parent = nullptr);
    // How many are selected, and whether their gaps can be shared per axis.
    void setSelection(int count, bool roomAcross, bool roomDown);
    void refreshTheme();
signals:
    void alignChosen(Align align);
    void distributeChosen(Qt::Orientation orientation);
private:
    QToolButton *m_align[6]{};
    QToolButton *m_distribute[2]{};
};
}
