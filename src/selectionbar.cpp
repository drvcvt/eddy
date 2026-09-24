#include "selectionbar.h"
#include "theme.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QToolButton>

namespace eddy {

namespace {
struct Choice { Align align; const char *icon; const char *tip; };
const Choice kAligns[] = {
    {Align::Left, "objects-left", "Align left edges"},
    {Align::HCenter, "objects-hcenter", "Align centres across"},
    {Align::Right, "objects-right", "Align right edges"},
    {Align::Top, "objects-top", "Align top edges"},
    {Align::VCenter, "objects-vcenter", "Align middles"},
    {Align::Bottom, "objects-bottom", "Align bottom edges"},
};
const char *kDistributeIcons[] = {"distribute-h", "distribute-v"};
const char *kDistributeTips[] = {"Space evenly across", "Space evenly down"};
}

SelectionBar::SelectionBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SelectionBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    for (int i = 0; i < 6; ++i) {
        m_align[i] = theme::floatButton(this, QStringLiteral("AlignObjects"), tr(kAligns[i].tip));
        connect(m_align[i], &QToolButton::clicked, this, [this, a = kAligns[i].align] { emit alignChosen(a); });
        layout->addWidget(m_align[i]);
        if (i == 2) layout->addSpacing(6);
    }
    layout->addSpacing(6);
    for (int i = 0; i < 2; ++i) {
        m_distribute[i] = theme::floatButton(this, QStringLiteral("DistributeObjects"), tr(kDistributeTips[i]));
        connect(m_distribute[i], &QToolButton::clicked, this, [this, i] {
            emit distributeChosen(i == 0 ? Qt::Horizontal : Qt::Vertical);
        });
        layout->addWidget(m_distribute[i]);
    }
    refreshTheme();
}

void SelectionBar::setSelection(int count, bool roomAcross, bool roomDown) {
    const bool room[] = {roomAcross, roomDown};
    for (int i = 0; i < 2; ++i) {
        m_distribute[i]->setVisible(count >= 3);
        m_distribute[i]->setEnabled(room[i]);
        const QString tip = tr(kDistributeTips[i]);
        m_distribute[i]->setToolTip(room[i] ? tip : tip + tr("\nNot enough room: they overlap"));
    }
}

void SelectionBar::refreshTheme() {
    const QPalette palette = QApplication::palette();
    const QColor rest = palette.color(QPalette::PlaceholderText);
    const QColor active = palette.color(QPalette::WindowText);
    for (int i = 0; i < 6; ++i)
        m_align[i]->setIcon(theme::tintedIcon(QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(kAligns[i].icon)),
                                              rest, active, theme::kFloatIcon));
    for (int i = 0; i < 2; ++i)
        m_distribute[i]->setIcon(theme::tintedIcon(
            QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(kDistributeIcons[i])), rest, active, theme::kFloatIcon));
}

}
