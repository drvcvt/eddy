#pragma once
#include <QWidget>
#include <QHash>
#include "items/redactitem.h"   // RedactMode

class QToolButton;

namespace eddy {

// Floating bar of the four redact modes. Lives as a child of the canvas viewport;
// EditorWindow shows/positions it over the selected redact item.
class RedactBar : public QWidget {
    Q_OBJECT
public:
    explicit RedactBar(QWidget *parent = nullptr);
    void setMode(RedactMode m);     // check the matching button (does not emit)

signals:
    void modeChosen(RedactMode m);

private:
    QHash<int, QToolButton *> m_btns;   // int(RedactMode) -> button
};

}
