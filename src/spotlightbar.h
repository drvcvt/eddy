#pragma once
#include <QWidget>
#include "items/spotlightitem.h"
class QToolButton;
namespace eddy {
class SpotlightBar : public QWidget {
    Q_OBJECT
public:
    explicit SpotlightBar(QWidget *parent = nullptr);
    void setValues(SpotlightShape shape, int intensity);
signals:
    void shapeChosen(SpotlightShape);
    void intensityChosen(int);
private:
    QToolButton *m_shapes[2]{};
    QToolButton *m_levels[3]{};
};
}
