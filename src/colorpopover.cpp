#include "colorpopover.h"
#include "theme.h"
#include <QGridLayout>
#include <QToolButton>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QPainter>
#include <QApplication>

namespace eddy {

ColorPopover::ColorPopover(QWidget *parent, const QColor &current) : QWidget(parent, Qt::Popup) {
    setObjectName("ColorPopover");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(6);

    auto *hex = new QLineEdit(current.name().toUpper(), this);
    hex->setObjectName("ColorHex");
    hex->setAccessibleName("Hex colour");
    hex->setToolTip("Hex colour · Enter to apply");
    hex->setValidator(new QRegularExpressionValidator(QRegularExpression("#[0-9a-fA-F]{6}"), hex));
    hex->setMaxLength(7);
    hex->setFixedHeight(theme::kFloatButton.height());
    connect(hex, &QLineEdit::returnPressed, this, [this, hex] {
        if (!hex->hasAcceptableInput()) return;
        emit picked(QColor(hex->text()));
        close();
    });
    grid->addWidget(hex, 0, 0, 1, 4);

    const QStringList presets = {
        "#ff3b30", "#ff9f0a", "#ffd60a", "#32d74b",
        "#0a84ff", "#ffffff", "#1a1a1a", "#cccccc",
    };
    for (int i = 0; i < presets.size(); ++i) {
        const QColor c(presets[i]);
        auto *b = new QToolButton(this);
        b->setObjectName("ColorPreset");
        b->setFixedSize(theme::kFloatButton);
        b->setCheckable(true);
        b->setChecked(c == current);
        b->setToolTip(c.name().toUpper());
        b->setAccessibleName(c.name().toUpper());
        b->setCursor(Qt::PointingHandCursor);
        const qreal dpr = devicePixelRatioF();
        QPixmap icon(qRound(theme::kFloatIcon * dpr), qRound(theme::kFloatIcon * dpr));
        icon.setDevicePixelRatio(dpr);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(c);
        painter.drawRoundedRect(QRectF(1, 1, 18, 18), 6, 6);
        if (b->isChecked()) {
            painter.setPen(QPen(c.lightness() > 140 ? QColor("#1a1a1a") : Qt::white,
                                2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolyline(QPolygonF{QPointF(5.5, 10), QPointF(8.2, 12.7), QPointF(14.5, 6.4)});
        }
        painter.end();
        b->setIcon(QIcon(icon));
        b->setIconSize(QSize(theme::kFloatIcon, theme::kFloatIcon));
        connect(b, &QToolButton::clicked, this, [this, c]{ emit picked(c); close(); });
        grid->addWidget(b, 1 + i / 4, i % 4);
    }
    auto *custom = new QToolButton(this);
    custom->setObjectName("Custom");
    custom->setText("More colours…");
    custom->setAccessibleName(custom->text());
    custom->setFixedHeight(theme::kFloatButton.height());
    custom->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    custom->setCursor(Qt::PointingHandCursor);
    connect(custom, &QToolButton::clicked, this, [this, current]{
        emit customRequested(current);
        close();
    });
    grid->addWidget(custom, 3, 0, 1, 4);

    auto *pipette = new QToolButton(this);
    pipette->setObjectName("Pipette");
    pipette->setText("Pick from image");
    pipette->setAccessibleName(pipette->text());
    pipette->setIcon(theme::tintedIcon(QStringLiteral(":/icons/eyedropper.svg"),
                                       QApplication::palette().color(QPalette::WindowText),
                                       QApplication::palette().color(QPalette::WindowText),
                                       theme::kFsSmall));
    pipette->setIconSize(QSize(theme::kFsSmall, theme::kFsSmall));
    pipette->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pipette->setFixedHeight(theme::kFloatButton.height());
    pipette->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pipette->setCursor(Qt::PointingHandCursor);
    connect(pipette, &QToolButton::clicked, this, [this]{
        emit eyedropperRequested();
        close();
    });
    grid->addWidget(pipette, 4, 0, 1, 4);
    setFixedWidth(sizeHint().width());
    setFocusPolicy(Qt::StrongFocus);
    setFocus(); // Presets first; the hex field only edits when explicitly focused.
}
}
