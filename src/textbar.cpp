#include "textbar.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QApplication>

namespace eddy {
static QToolButton *textButton(QWidget *parent, const QString &label, bool checkable = true) {
    auto *button = new QToolButton(parent);
    button->setText(label);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(26);
    return button;
}

TextBar::TextBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("TextBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6,4,6,4);
    layout->setSpacing(2);

    auto *sizes = new QButtonGroup(this); sizes->setExclusive(true);
    const qreal points[] = {14, 20, 28};
    for (int i = 0; i < 3; ++i) {
        m_sizes[i] = textButton(this, QString());
        m_sizes[i]->setObjectName(QStringLiteral("TextSize%1").arg(points[i]));
        m_sizes[i]->setToolTip(QStringLiteral("%1 pt text size").arg(points[i]));
        m_sizes[i]->setAccessibleName(m_sizes[i]->toolTip());
        m_sizes[i]->setFixedWidth(32);
        m_sizes[i]->setIconSize(QSize(16,16));
        sizes->addButton(m_sizes[i]); layout->addWidget(m_sizes[i]);
        connect(m_sizes[i], &QToolButton::clicked, this, [this, size=points[i]]{ emit sizeChosen(size); });
    }
    m_bold = textButton(this, QString());
    m_bold->setObjectName(QStringLiteral("TextBold"));
    m_bold->setToolTip(QStringLiteral("Bold text"));
    m_bold->setAccessibleName(m_bold->toolTip());
    m_bold->setFixedWidth(32);
    m_bold->setIconSize(QSize(16,16));
    connect(m_bold, &QToolButton::toggled, this, &TextBar::boldChosen);
    layout->addWidget(m_bold);

    auto *alignments = new QButtonGroup(this); alignments->setExclusive(true);
    const Qt::Alignment values[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
    const char *alignNames[] = {"Left", "Center", "Right"};
    for (int i = 0; i < 3; ++i) {
        m_align[i] = textButton(this, QString());
        m_align[i]->setObjectName(QStringLiteral("TextAlign%1").arg(QString::fromLatin1(alignNames[i])));
        m_align[i]->setToolTip(QStringLiteral("Align %1").arg(QString::fromLatin1(alignNames[i]).toLower()));
        m_align[i]->setAccessibleName(m_align[i]->toolTip());
        m_align[i]->setFixedWidth(32);
        m_align[i]->setIconSize(QSize(16,16));
        alignments->addButton(m_align[i]); layout->addWidget(m_align[i]);
        connect(m_align[i], &QToolButton::clicked, this, [this, value=values[i]]{ emit alignmentChosen(value); });
    }
    m_filled = textButton(this, QString());
    m_filled->setObjectName(QStringLiteral("TextFill"));
    m_filled->setToolTip(QStringLiteral("Filled label background"));
    m_filled->setAccessibleName(m_filled->toolTip());
    m_filled->setFixedWidth(32);
    m_filled->setIconSize(QSize(16,16));
    connect(m_filled, &QToolButton::toggled, this, [this](bool on){
        emit styleChosen(on ? TextLabelStyle::Filled : TextLabelStyle::Plain);
    });
    layout->addWidget(m_filled);
    refreshTheme();
}

void TextBar::refreshTheme() {
    const QPalette palette = QApplication::palette();
    const QColor rest = palette.color(QPalette::WindowText);
    const QColor active = palette.color(QPalette::WindowText);
    const char *sizeIcons[] = {"text-small", "text-medium", "text-large"};
    for (int i = 0; i < 3; ++i)
        m_sizes[i]->setIcon(theme::tintedIcon(
            QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(sizeIcons[i])), rest, active));
    m_bold->setIcon(theme::tintedIcon(QStringLiteral(":/icons/text-bold.svg"), rest, active));
    const char *icons[] = {"left", "center", "right"};
    for (int i = 0; i < 3; ++i)
        m_align[i]->setIcon(theme::tintedIcon(
            QStringLiteral(":/icons/align-%1.svg").arg(QString::fromLatin1(icons[i])), rest, active));
    m_filled->setIcon(theme::tintedIcon(QStringLiteral(":/icons/label-fill.svg"), rest, active));
}

void TextBar::setState(const TextState &state) {
    const qreal size = state.font.pointSizeF();
    m_sizes[size < 17 ? 0 : size < 24 ? 1 : 2]->setChecked(true);
    m_bold->setChecked(state.font.bold());
    m_align[state.alignment.testFlag(Qt::AlignRight) ? 2
            : state.alignment.testFlag(Qt::AlignHCenter) ? 1 : 0]->setChecked(true);
    m_filled->setChecked(state.labelStyle == TextLabelStyle::Filled);
}
}
