#pragma once
#include <QWidget>
#include "toolcontroller.h"
class QToolButton;
namespace eddy {
class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget *parent=nullptr);
signals:
    void toolChosen(ToolType t);
    void colorChosen(const QColor &c);
    void saveRequested();
    void copyRequested();
};
}
