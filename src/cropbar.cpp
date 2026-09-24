#include "cropbar.h"
#include "theme.h"
#include <QActionGroup>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>

namespace eddy {
CropBar::CropBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("CropBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    auto button = [&](const QString &text, const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        layout->addWidget(b, 0, m_controls.size());
        m_controls.append(b);
        return b;
    };
    m_ratio = button(tr("Free"), QStringLiteral("CropRatio"), tr("Crop aspect ratio"));
    m_ratio->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_ratio);
    menu->setObjectName(QStringLiteral("CropRatioMenu"));
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    auto *group = new QActionGroup(menu);
    const QString labels[] = {tr("Free"), tr("Original"), QStringLiteral("16:9"),
                              QStringLiteral("9:16"), QStringLiteral("1:1")};
    const qreal ratios[] = {0, -1, 16.0 / 9, 9.0 / 16, 1};
    for (int i = 0; i < 5; ++i) {
        auto *action = menu->addAction(labels[i]);
        action->setCheckable(true);
        action->setChecked(i == 0);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, action, ratio = ratios[i]] {
            theme::setMenuLabel(m_ratio, action->text());
            emit ratioChosen(ratio < 0 ? qreal(m_sourceSize.width()) / qMax(1, m_sourceSize.height()) : ratio);
        });
    }
    m_ratio->setMenu(menu);
    theme::setMenuArrow(m_ratio);
    m_size = new QLabel(this);
    m_size->setObjectName(QStringLiteral("CropSize"));
    m_size->setToolTip(tr("Cropped output size in pixels"));
    layout->addWidget(m_size, 0, m_controls.size());
    m_controls.append(m_size);
    connect(button(tr("Reset"), QStringLiteral("CropReset"), tr("Restore the full image")),
            &QToolButton::clicked, this, &CropBar::resetRequested);
    connect(button(tr("Cancel"), QStringLiteral("CropCancel"), tr("Cancel crop\tEsc")),
            &QToolButton::clicked, this, &CropBar::cancelRequested);
    connect(button(tr("Apply"), QStringLiteral("CropApply"), tr("Apply crop\tEnter")),
            &QToolButton::clicked, this, &CropBar::applyRequested);
}
void CropBar::setOutputSize(QSize size) {
    m_size->setText(QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
    m_size->setAccessibleName(tr("Output size: %1 by %2 pixels").arg(size.width()).arg(size.height()));
    adjustSize();
}
void CropBar::setAvailableWidth(int width) {
    int horizontal = 8 + 2 * (m_controls.size() - 1);
    for (auto *control : m_controls) horizontal += control->sizeHint().width();
    const bool narrow = width < horizontal;
    if (narrow != m_narrow) {
        m_narrow = narrow;
        auto *grid = qobject_cast<QGridLayout *>(layout());
        for (auto *control : m_controls) grid->removeWidget(control);
        for (int i = 0; i < m_controls.size(); ++i) {
            if (!narrow) grid->addWidget(m_controls[i], 0, i);
            else if (i == 0) grid->addWidget(m_controls[i], 0, 0);
            else if (i == 1) grid->addWidget(m_controls[i], 0, 1, 1, 2);
            else grid->addWidget(m_controls[i], 1, i - 2);
        }
    }
    adjustSize();
}
void CropBar::resetRatio() {
    m_ratio->menu()->actions().first()->setChecked(true);
    theme::setMenuLabel(m_ratio, tr("Free"));
}
}
