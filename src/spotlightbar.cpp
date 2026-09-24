#include "spotlightbar.h"
#include "theme.h"
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
    result->setFixedHeight(theme::kFloatButton.height());
    return result;
}

SpotlightBar::SpotlightBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SpotlightBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
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
    // Videos: the whole clip or from the playhead on (studio plan 6.7, E5).
    auto *scopes = new QButtonGroup(this);
    for (int i = 0; i < 2; ++i) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("TimeScope"));
        b->setText(i ? tr("From playhead") : tr("Whole clip"));
        b->setToolTip(i ? tr("Show it from the playhead to the end, then shorten it on the mask lane")
                        : tr("Show it for the whole clip"));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        b->hide();
        scopes->addButton(b, i);
        layout->addWidget(b);
        m_scopes[i] = b;
    }
    connect(scopes, &QButtonGroup::idClicked, this, [this](int id) { emit timeScopeChosen(id == 1); });
}

void SpotlightBar::setTimeScope(bool video, bool timed) {
    for (int i = 0; i < 2; ++i) {
        m_scopes[i]->setVisible(video);
        m_scopes[i]->setChecked(timed == (i == 1));
    }
    adjustSize();
}

void SpotlightBar::setValues(SpotlightShape shape, int intensity) {
    m_shapes[shape == SpotlightShape::Ellipse]->setChecked(true);
    m_levels[qBound(1, intensity, 3) - 1]->setChecked(true);
}

}
