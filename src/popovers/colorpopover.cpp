#include "popovers/colorpopover.h"
#include "app/theme.h"
#include <QGridLayout>
#include <QToolButton>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QPainter>
#include <QApplication>
#include <QStyle>
#include <QStyleOption>

namespace eddy {

ColorPopover::ColorPopover(QWidget *parent, const QColor &current)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
    setObjectName("ColorPopover");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);   // the stylesheet's r-pop corners, not a square window
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(theme::kS3, theme::kS3, theme::kS3, theme::kS3);
    grid->setHorizontalSpacing(theme::kS1);
    grid->setVerticalSpacing(theme::kS1);

    auto *hex = new QLineEdit(current.name().toUpper(), this);
    hex->setObjectName("ColorHex");
    hex->setAccessibleName("Hex colour");
    hex->setToolTip("Hex colour, Enter applies");
    hex->setValidator(new QRegularExpressionValidator(QRegularExpression("#[0-9a-fA-F]{6}"), hex));
    hex->setMaxLength(7);
    hex->setFixedHeight(theme::kCtl);
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
        b->setFixedSize(theme::kControl);
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
        // A squircle like every mark, ringed like the top bar's colour disc, so the
        // near-black and white presets still show on the popover.
        QColor ring = QApplication::palette().color(QPalette::WindowText);
        ring.setAlpha(90);
        painter.setPen(QPen(ring, 1));
        painter.setBrush(c);
        painter.drawPath(theme::squircle(QRectF(1.5, 1.5, 17, 17)));
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
    custom->setFixedHeight(theme::kCtl);
    custom->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    custom->setCursor(Qt::PointingHandCursor);
    connect(custom, &QToolButton::clicked, this, [this, current]{
        emit customRequested(current);
        close();
    });
    grid->addWidget(custom, 3, 0, 1, 4);

    auto *pipette = new theme::CapButton(this);   // icon on the capitals, even padding
    pipette->setObjectName("Pipette");
    pipette->setText("Pick from image");
    pipette->setAccessibleName(pipette->text());
    pipette->setIcon(theme::tintedIcon(QStringLiteral(":/icons/eyedropper.svg"),
                                       QApplication::palette().color(QPalette::WindowText),
                                       QApplication::palette().color(QPalette::WindowText),
                                       theme::kFsUi));
    pipette->setIconSize(QSize(theme::kFsUi, theme::kFsUi));
    pipette->setFixedHeight(theme::kCtl);
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

void ColorPopover::paintEvent(QPaintEvent *) {
    // A translucent popup does not get its stylesheet background on its own.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}
}
