#include "toolbar.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QColorDialog>

namespace eddy {

static QToolButton *toolBtn(const QString &label, bool checkable=true) {
    auto *b = new QToolButton; b->setText(label); b->setCheckable(checkable);
    b->setAutoRaise(true); return b;
}

Toolbar::Toolbar(QWidget *parent) : QWidget(parent) {
    setObjectName("Toolbar");
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(8,4,8,4); lay->setSpacing(2);
    auto *group = new QButtonGroup(this); group->setExclusive(true);

    // Build a checkable button for each tool:
    const QList<QPair<QString,ToolType>> tools = {
        {"M", ToolType::Move}, {"A", ToolType::Arrow}, {"P", ToolType::Pen},
        {"R", ToolType::Rect}, {"E", ToolType::Ellipse}, {"H", ToolType::Highlight},
        {"T", ToolType::Text}, {"B", ToolType::Blur}, {"X", ToolType::Redact},
    };
    for (const auto &pair : tools) {
        auto *b = toolBtn(pair.first); group->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, t=pair.second]{ emit toolChosen(t); });
        lay->addWidget(b);
    }
    auto *color = toolBtn("C", false);
    connect(color, &QToolButton::clicked, this, [this]{
        QColor c = QColorDialog::getColor();
        if (c.isValid()) emit colorChosen(c);
    });
    lay->addWidget(color);
    auto *save = toolBtn("Save", false);
    connect(save, &QToolButton::clicked, this, [this]{ emit saveRequested(); });
    lay->addWidget(save);
    auto *copy = toolBtn("Copy", false);
    connect(copy, &QToolButton::clicked, this, [this]{ emit copyRequested(); });
    lay->addWidget(copy);
}

}
