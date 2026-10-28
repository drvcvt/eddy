#include "timeline/bars/fragmentbar.h"
#include "app/theme.h"
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

namespace eddy {

FragmentBar::FragmentBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("FragmentBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(theme::kS1, theme::kS1, theme::kS1, theme::kS1);
    layout->setSpacing(theme::kS1);
    auto button = [&](const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAccessibleName(theme::spokenName(tip));
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kCtl);
        layout->addWidget(b);
        return b;
    };
    m_speed = button(QStringLiteral("FragmentSpeed"), tr("Speed of this piece"));
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
    m_join = button(QStringLiteral("FragmentJoin"), tr("Join with the piece before"));
    m_join->setText(tr("Join"));
    connect(m_join, &QToolButton::clicked, this, &FragmentBar::joinRequested);
    refreshTheme();
}

void FragmentBar::setFragment(const Fragment &fragment, bool canJoin) {
    theme::setMenuLabel(m_speed, QString::number(fragment.speed, 'g', 3) + QStringLiteral("×"));
    for (QAction *a : m_speed->menu()->actions()) a->setChecked(qFuzzyCompare(a->data().toDouble(), fragment.speed));
    // Every control keeps its slot; what does not apply is disabled, not hidden, so the
    // bar keeps one width from one fragment to the next.
    m_speed->setEnabled(!fragment.removed);
    m_speed->setToolTip(fragment.removed ? tr("A cut piece has no speed") : tr("Speed of this piece"));
    m_join->setEnabled(canJoin && !fragment.removed);
    m_join->setToolTip(canJoin && !fragment.removed ? tr("Join with the piece before")
                                                   : tr("Joins only with a piece right before it"));
    adjustSize();
}

void FragmentBar::refreshTheme() {
    theme::setMenuArrow(m_speed);
}

}
