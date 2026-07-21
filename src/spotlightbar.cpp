#include "spotlightbar.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>

namespace eddy {

static QToolButton *button(QWidget *parent, const char *label) {
    auto *result = new QToolButton(parent);
    result->setText(label);
    result->setCheckable(true);
    result->setAutoRaise(true);
    result->setFocusPolicy(Qt::NoFocus);
    result->setFixedHeight(26);
    return result;
}

SpotlightBar::SpotlightBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SpotlightBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);

    auto *shapeGroup = new QButtonGroup(this);
    shapeGroup->setExclusive(true);
    m_shapes[0] = button(this, "Round");
    m_shapes[1] = button(this, "Oval");
    for (int i = 0; i < 2; ++i) {
        shapeGroup->addButton(m_shapes[i]);
        layout->addWidget(m_shapes[i]);
        connect(m_shapes[i], &QToolButton::clicked, this, [this, i] {
            emit shapeChosen(i ? SpotlightShape::Ellipse : SpotlightShape::RoundedRect);
        });
    }

    auto *levelGroup = new QButtonGroup(this);
    levelGroup->setExclusive(true);
    for (int i = 0; i < 3; ++i) {
        m_levels[i] = button(this, i == 0 ? "Low" : i == 1 ? "Mid" : "High");
        levelGroup->addButton(m_levels[i]);
        layout->addWidget(m_levels[i]);
        connect(m_levels[i], &QToolButton::clicked, this, [this, i] {
            emit intensityChosen(i + 1);
        });
    }
}

void SpotlightBar::setValues(SpotlightShape shape, int intensity) {
    m_shapes[shape == SpotlightShape::Ellipse]->setChecked(true);
    m_levels[qBound(1, intensity, 3) - 1]->setChecked(true);
}

}
