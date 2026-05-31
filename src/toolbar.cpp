#include "toolbar.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QFrame>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QResizeEvent>

namespace eddy {

static QToolButton *mkBtn(bool checkable, bool square) {
    auto *b = new QToolButton;
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);      // keep keyboard focus on the window for hotkeys
    b->setCursor(Qt::PointingHandCursor);
    if (square) b->setFixedSize(34, 34);
    else b->setFixedHeight(30);
    return b;
}

Toolbar::Toolbar(QWidget *parent) : QWidget(parent) {
    setObjectName("Toolbar");
    setAttribute(Qt::WA_StyledBackground, true);

    // The sliding accent pill lives behind the buttons.
    m_pill = new QWidget(this);
    m_pill->setObjectName("Pill");
    m_pill->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_pill->setFixedSize(34, 34);
    m_pill->hide();
    m_pillAnim = new QPropertyAnimation(m_pill, "geometry", this);
    m_pillAnim->setDuration(170);
    m_pillAnim->setEasingCurve(QEasingCurve::OutBack);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(4);
    auto *group = new QButtonGroup(this); group->setExclusive(true);

    struct T { ToolType type; const char *id; const char *name; const char *key; };
    const QVector<T> tools = {
        {ToolType::Move,"move","Move","M"}, {ToolType::Arrow,"arrow","Arrow","A"},
        {ToolType::Pen,"pen","Pen","P"}, {ToolType::Rect,"rect","Rectangle","R"},
        {ToolType::Ellipse,"ellipse","Ellipse","E"}, {ToolType::Highlight,"highlight","Highlight","H"},
        {ToolType::Text,"text","Text","T"}, {ToolType::Blur,"blur","Blur","B"},
        {ToolType::Redact,"redact","Redact","X"},
    };
    for (const T &t : tools) {
        auto *b = mkBtn(true, true);
        b->setIcon(theme::tintedIcon(QString(":/icons/%1.svg").arg(t.id),
                                     QColor(theme::kIconRest), QColor(theme::kIconActive)));
        b->setIconSize(QSize(20, 20));
        b->setToolTip(QString("%1 \xC2\xB7 %2").arg(t.name, t.key));
        group->addButton(b);
        m_btns.insert(int(t.type), b);
        connect(b, &QToolButton::clicked, this, [this, tt=t.type]{ emit toolChosen(tt); });
        lay->addWidget(b);
    }

    lay->addStretch(1);

    auto *color = mkBtn(false, true);
    color->setObjectName("Swatch");
    color->setText(QString::fromUtf8("\xE2\x97\x8F"));   // round swatch
    color->setToolTip("Stroke colour");
    connect(color, &QToolButton::clicked, this, [this]{
        QColor c = QColorDialog::getColor();
        if (c.isValid()) emit colorChosen(c);
    });
    lay->addWidget(color);

    auto *sep = new QFrame; sep->setObjectName("Sep");
    sep->setFrameShape(QFrame::VLine); sep->setFixedHeight(20);
    lay->addWidget(sep);

    auto *save = mkBtn(false, false); save->setObjectName("Save");
    save->setText("Save"); save->setToolTip("Save \xC2\xB7 Enter");
    connect(save, &QToolButton::clicked, this, [this]{ emit saveRequested(); });
    lay->addWidget(save);

    auto *copy = mkBtn(false, false); copy->setObjectName("Copy");
    copy->setText("Copy"); copy->setToolTip("Copy to clipboard \xC2\xB7 Ctrl+C");
    connect(copy, &QToolButton::clicked, this, [this]{ emit copyRequested(); });
    lay->addWidget(copy);

    syncTool(ToolType::Arrow);   // sensible default highlight (snaps on first show)
}

void Toolbar::syncTool(ToolType t) {
    auto *b = m_btns.value(int(t), nullptr);
    if (!b) return;
    b->setChecked(true);                 // exclusive group unchecks the rest
    m_active = b;
    movePillTo(b, m_anim && isVisible());
}

void Toolbar::movePillTo(QToolButton *b, bool animate) {
    if (!b) return;
    const QRect target = b->geometry();
    m_pill->show();
    m_pill->lower();                     // behind buttons, above the bar background
    if (animate) {
        m_pillAnim->stop();
        m_pillAnim->setStartValue(m_pill->geometry());
        m_pillAnim->setEndValue(target);
        m_pillAnim->start();
    } else {
        m_pill->setGeometry(target);
    }
}

void Toolbar::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (m_active) movePillTo(m_active, false);   // first real layout: snap
}

void Toolbar::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (m_active) movePillTo(m_active, false);
}

}
