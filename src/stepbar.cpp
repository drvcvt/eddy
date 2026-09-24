#include "stepbar.h"
#include "theme.h"
#include <QApplication>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

namespace eddy {

StepBar::StepBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("StepBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    m_number = new QLineEdit(this);
    m_number->setObjectName(QStringLiteral("StepNumber"));
    m_number->setValidator(new QIntValidator(1, StepItem::kMaxNumber, m_number));
    m_number->setAlignment(Qt::AlignCenter);
    m_number->setFixedSize(44, theme::kFloatButton.height());
    m_number->setToolTip(tr("Step number\nApply\tEnter\nCancel\tEsc"));
    m_number->setAccessibleName(tr("Step number"));
    m_number->installEventFilter(this);
    connect(m_number, &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const int number = m_number->text().toInt(&ok);
        if (ok && number != m_current) emit numberChosen(number);
        else m_number->setText(QString::number(m_current));
    });
    layout->addWidget(m_number);

    auto *sizes = new QButtonGroup(this);
    sizes->setExclusive(true);
    const StepItem::Size values[] = {StepItem::Size::S, StepItem::Size::M, StepItem::Size::L};
    const char *names[] = {"Small", "Medium", "Large"};
    for (int i = 0; i < 3; ++i) {
        auto *button = theme::floatButton(this, QStringLiteral("StepSize"), tr("%1 step").arg(QString::fromLatin1(names[i])));
        button->setCheckable(true);
        sizes->addButton(button);
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, size = values[i]] { emit sizeChosen(size); });
        m_sizes[i] = button;
    }

    m_more = theme::floatButton(this, QStringLiteral("StepMore"), tr("More step actions"));
    m_more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_more);
    menu->addAction(tr("Renumber by creation order"), this, &StepBar::renumberRequested);
    m_more->setMenu(menu);
    layout->addWidget(m_more);
    refreshTheme();
}

void StepBar::setStep(int number, StepItem::Size size) {
    m_current = number;
    if (!m_number->hasFocus()) m_number->setText(QString::number(number));
    m_sizes[size == StepItem::Size::S ? 0 : size == StepItem::Size::L ? 2 : 1]->setChecked(true);
}

void StepBar::refreshTheme() {
    const QPalette palette = QApplication::palette();
    const QColor rest = palette.color(QPalette::PlaceholderText);
    const QColor active = palette.color(QPalette::WindowText);
    const char *letters[] = {"letter-s", "letter-m", "letter-l"};
    for (int i = 0; i < 3; ++i)
        m_sizes[i]->setIcon(theme::tintedIcon(QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(letters[i])),
                                              rest, active, theme::kFloatIcon));
    m_more->setIcon(theme::tintedIcon(QStringLiteral(":/icons/menu-arrow.svg"), rest, active, theme::kFloatIcon));
}

bool StepBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_number && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_number->setText(QString::number(m_current));
        m_number->clearFocus();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

}
