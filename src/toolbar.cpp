#include "toolbar.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QFrame>

namespace eddy {

static QToolButton *toolBtn(const QString &label, bool checkable, bool square) {
    auto *b = new QToolButton;
    b->setText(label);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);      // keep keyboard focus on the window for hotkeys
    b->setCursor(Qt::PointingHandCursor);
    if (square) b->setFixedSize(30, 30);
    else b->setFixedHeight(30);
    return b;
}

Toolbar::Toolbar(QWidget *parent) : QWidget(parent) {
    setObjectName("Toolbar");
    setAttribute(Qt::WA_StyledBackground, true);   // let QSS paint the bar background
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(4);
    auto *group = new QButtonGroup(this); group->setExclusive(true);

    // Tool buttons show their keybind letter — minimal, self-documenting.
    const QList<QPair<QString,ToolType>> tools = {
        {"M", ToolType::Move}, {"A", ToolType::Arrow}, {"P", ToolType::Pen},
        {"R", ToolType::Rect}, {"E", ToolType::Ellipse}, {"H", ToolType::Highlight},
        {"T", ToolType::Text}, {"B", ToolType::Blur}, {"X", ToolType::Redact},
    };
    for (const auto &pair : tools) {
        auto *b = toolBtn(pair.first, true, true);
        if (pair.second == ToolType::Arrow) b->setChecked(true); // default tool
        group->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, t=pair.second]{ emit toolChosen(t); });
        lay->addWidget(b);
    }

    lay->addStretch(1);

    auto *color = toolBtn(QString::fromUtf8("\xE2\x97\x8F"), false, true); // ● swatch
    color->setObjectName("Swatch");
    connect(color, &QToolButton::clicked, this, [this]{
        QColor c = QColorDialog::getColor();
        if (c.isValid()) emit colorChosen(c);
    });
    lay->addWidget(color);

    auto *sep = new QFrame; sep->setObjectName("Sep");
    sep->setFrameShape(QFrame::VLine); sep->setFixedHeight(20);
    lay->addWidget(sep);

    auto *save = toolBtn("Save", false, false);
    save->setObjectName("Save");
    connect(save, &QToolButton::clicked, this, [this]{ emit saveRequested(); });
    lay->addWidget(save);

    auto *copy = toolBtn("Copy", false, false);
    copy->setObjectName("Copy");
    connect(copy, &QToolButton::clicked, this, [this]{ emit copyRequested(); });
    lay->addWidget(copy);
}

}
