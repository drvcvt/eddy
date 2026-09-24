#include "fragmentbar.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

namespace eddy {

FragmentBar::FragmentBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("FragmentBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    auto button = [&](const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        layout->addWidget(b);
        return b;
    };
    m_speed = button(QStringLiteral("FragmentSpeed"), tr("Speed of this fragment"));
    m_speed->setPopupMode(QToolButton::InstantPopup);
    auto *speeds = new QMenu(m_speed);
    speeds->setObjectName(QStringLiteral("FragmentSpeedMenu"));
    speeds->setWindowFlag(Qt::FramelessWindowHint);
    speeds->setAttribute(Qt::WA_TranslucentBackground);
    for (double s : {0.25, 0.5, 1.0, 1.5, 2.0, 4.0}) {
        auto *action = speeds->addAction(QString::number(s, 'g', 3) + QStringLiteral("×"));
        action->setCheckable(true);
        action->setData(s);
        connect(action, &QAction::triggered, this, [this, s] { emit speedChosen(s); });
    }
    m_speed->setMenu(speeds);
    m_cut = button(QStringLiteral("FragmentCut"), tr("Leave this fragment out\tDelete"));
    connect(m_cut, &QToolButton::clicked, this, &FragmentBar::cutToggled);
    m_join = button(QStringLiteral("FragmentJoin"), tr("Join with the fragment before"));
    m_join->setText(tr("Join"));
    connect(m_join, &QToolButton::clicked, this, &FragmentBar::joinRequested);
    refreshTheme();
}

void FragmentBar::setFragment(const Fragment &fragment, bool canJoin) {
    theme::setMenuLabel(m_speed, QString::number(fragment.speed, 'g', 3) + QStringLiteral("×"));
    for (QAction *a : m_speed->menu()->actions()) a->setChecked(qFuzzyCompare(a->data().toDouble(), fragment.speed));
    m_speed->setVisible(!fragment.removed);
    m_cut->setText(fragment.removed ? tr("Restore") : tr("Cut"));
    m_cut->setToolTip(fragment.removed ? tr("Bring this fragment back") : tr("Leave this fragment out\tDelete"));
    m_join->setVisible(canJoin && !fragment.removed);
    adjustSize();
}

void FragmentBar::refreshTheme() {
    theme::setMenuArrow(m_speed);
}

}
