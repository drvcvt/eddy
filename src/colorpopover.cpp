#include "colorpopover.h"
#include "theme.h"
#include <QGridLayout>
#include <QToolButton>
#include <QColorDialog>
#include <QIcon>

namespace eddy {

ColorPopover::ColorPopover(QWidget *parent) : QWidget(parent, Qt::Popup) {
    setObjectName("ColorPopover");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(6);

    const QStringList presets = {
        "#ff3b30", "#ff9f0a", "#ffd60a", "#32d74b",
        "#0a84ff", "#ffffff", "#1a1a1a", "#cccccc",
    };
    for (int i = 0; i < presets.size(); ++i) {
        auto *b = new QToolButton;
        b->setFixedSize(24, 24);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString("QToolButton{border:1px solid #2a2a2a;"
                                 "border-radius:5px;background:%1;}").arg(presets[i]));
        const QColor c(presets[i]);
        connect(b, &QToolButton::clicked, this, [this, c]{ emit picked(c); close(); });
        grid->addWidget(b, i / 4, i % 4);
    }
    auto *custom = new QToolButton;
    custom->setObjectName("Custom");
    custom->setText("Custom\xE2\x80\xA6");      // "Custom…"
    custom->setFixedHeight(24);
    custom->setCursor(Qt::PointingHandCursor);
    connect(custom, &QToolButton::clicked, this, [this]{
        close();
        QColor c = QColorDialog::getColor();
        if (c.isValid()) emit picked(c);
    });
    grid->addWidget(custom, 2, 0, 1, 4);

    auto *pipette = new QToolButton;
    pipette->setObjectName("Custom");           // reuse the Custom button styling
    pipette->setText("Pipette");
    pipette->setIcon(theme::tintedIcon(QStringLiteral(":/icons/eyedropper.svg"),
                                       QColor("#d0d0d0"), QColor("#ffffff")));
    pipette->setIconSize(QSize(15, 15));
    pipette->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pipette->setFixedHeight(24);
    pipette->setToolTip("Pick a colour from the image");
    pipette->setCursor(Qt::PointingHandCursor);
    connect(pipette, &QToolButton::clicked, this, [this]{
        close();
        emit eyedropperRequested();
    });
    grid->addWidget(pipette, 3, 0, 1, 4);
}
}
