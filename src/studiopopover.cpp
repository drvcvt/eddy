#include "studiopopover.h"
#include "theme.h"
#include <QApplication>
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QStyleOption>
#include <QToolButton>

namespace eddy {

static QIcon swatchIcon(const StudioStyle &style, qreal dpr) {
    const int size = theme::kFloatIcon;
    QPixmap pixmap(qRound(size * dpr), qRound(size * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(1, 1, size - 2, size - 2), 6, 6);
    if (style.active()) {
        const QImage bg = renderStudioBackground(QSize(100, 100), [&] {
            StudioStyle flat = style;
            flat.shadow = 0;
            return flat;
        }());
        p.setClipPath(shape);
        p.drawImage(QRectF(1, 1, size - 2, size - 2), bg);
    } else {
        // "None": the empty squircle with a diagonal, in the icon ink colour.
        const QColor ink = QApplication::palette().color(QPalette::WindowText);
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(shape);
        p.drawLine(QPointF(5, size - 5), QPointF(size - 5, 5));
    }
    return QIcon(pixmap);
}

StudioPopover::StudioPopover(const StudioStyle &style, QSize content, QWidget *parent)
    : QWidget(parent, Qt::Popup), m_style(style), m_content(content) {
    setObjectName(QStringLiteral("StudioPopover"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(6);
    int row = 0;
    auto section = [&](const QString &text) {
        auto *label = new QLabel(text.toUpper(), this);
        label->setObjectName(QStringLiteral("StudioSection"));
        QFont font = label->font();   // QSS has no letter-spacing
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        label->setFont(font);
        grid->addWidget(label, row++, 0, 1, 8);
    };

    section(tr("Background"));
    auto *backgrounds = new QButtonGroup(this);
    const qreal dpr = devicePixelRatioF();
    QList<StudioStyle> choices{StudioStyle()};
    for (const auto &preset : studioBackgroundPresets()) {
        StudioStyle s = m_style;
        s.background = preset.kind;
        s.color = preset.color;
        s.color2 = preset.color2;
        choices.append(s);
    }
    QStringList names{tr("None")};
    for (const auto &preset : studioBackgroundPresets()) names.append(preset.name);
    for (int i = 0; i < choices.size(); ++i) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("StudioBackground"));
        b->setCheckable(true);
        b->setFixedSize(theme::kFloatButton);
        b->setIcon(swatchIcon(choices[i], dpr));
        b->setIconSize(QSize(theme::kFloatIcon, theme::kFloatIcon));
        b->setToolTip(names[i]);
        b->setAccessibleName(tr("Background: %1").arg(names[i]));
        b->setCursor(Qt::PointingHandCursor);
        b->setChecked(i == 0 ? !m_style.active()
            : m_style.background == choices[i].background && m_style.color == choices[i].color
                && m_style.color2 == choices[i].color2);
        backgrounds->addButton(b, i);
        grid->addWidget(b, row, i);
        connect(b, &QToolButton::clicked, this, [this, s = choices[i]] {
            m_style.background = s.background;
            m_style.color = s.color;
            m_style.color2 = s.color2;
            apply();
        });
        m_backgrounds.append(b);
    }
    auto *image = new QToolButton(this);
    image->setObjectName(QStringLiteral("StudioImage"));
    image->setText(tr("Image…"));
    image->setToolTip(tr("Use an image as background"));
    image->setFixedHeight(theme::kFloatButton.height());
    image->setCursor(Qt::PointingHandCursor);
    connect(image, &QToolButton::clicked, this, &StudioPopover::imageRequested);
    grid->addWidget(image, row++, choices.size());

    auto slider = [&](const QString &text, double value, double max, auto setter) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("StudioLabel"));
        grid->addWidget(label, row, 0, 1, 3);
        auto *s = new QSlider(Qt::Horizontal, this);
        s->setRange(0, 100);
        s->setValue(qRound(value / max * 100));
        s->setFixedHeight(theme::kFloatButton.height());
        s->setAccessibleName(text);
        s->setCursor(Qt::PointingHandCursor);
        grid->addWidget(s, row++, 3, 1, choices.size() - 2);
        connect(s, &QSlider::valueChanged, this, [this, max, setter](int v) {
            setter(m_style, v / 100.0 * max);
            apply();
        });
        return s;
    };
    m_padding = slider(tr("Padding"), m_style.padding, 20, [](StudioStyle &s, double v) { s.padding = v; });
    m_radius = slider(tr("Corners"), m_style.radius, 6, [](StudioStyle &s, double v) { s.radius = v; });
    m_shadow = slider(tr("Shadow"), m_style.shadow, 100, [](StudioStyle &s, double v) { s.shadow = v; });

    grid->setRowMinimumHeight(row++, 6);   // with the 6px row gap: a 12px section break
    section(tr("Ratio"));
    auto *ratios = new QButtonGroup(this);
    const QList<QPair<QString, QSize>> ratioChoices{
        {tr("Auto"), QSize()}, {QStringLiteral("16:9"), QSize(16, 9)}, {QStringLiteral("4:3"), QSize(4, 3)},
        {QStringLiteral("1:1"), QSize(1, 1)}, {QStringLiteral("9:16"), QSize(9, 16)}};
    auto *ratioRow = new QHBoxLayout;
    ratioRow->setSpacing(4);
    for (const auto &[label, ratio] : ratioChoices) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("StudioRatio"));
        b->setText(label);
        b->setCheckable(true);
        b->setChecked(m_style.aspect == ratio);
        b->setFixedHeight(theme::kFloatButton.height());
        b->setAccessibleName(tr("Output ratio %1").arg(label));
        b->setCursor(Qt::PointingHandCursor);
        ratios->addButton(b);
        ratioRow->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, ratio] { m_style.aspect = ratio; apply(); });
        m_ratios.append(b);
    }
    ratioRow->addStretch(1);
    // Studio grows the file beyond the original; say by how much.
    m_size = new QLabel(this);
    m_size->setObjectName(QStringLiteral("StudioSize"));
    m_size->setToolTip(tr("Output size in pixels"));
    ratioRow->addWidget(m_size);
    grid->addLayout(ratioRow, row++, 0, 1, 8);
    apply();
    setFixedSize(sizeHint());
}

void StudioPopover::paintEvent(QPaintEvent *) {
    // A translucent popup (for the rounded corners) does not get its QSS
    // background painted on its own.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void StudioPopover::apply() {
    // Spacing only matters once there is a background to show it.
    for (QSlider *s : {m_padding, m_radius, m_shadow})
        if (s) s->setEnabled(m_style.active());
    for (QToolButton *b : std::as_const(m_ratios)) b->setEnabled(m_style.active());
    if (m_size) {
        const QSize out = studioLayout(m_content, m_style).output;
        m_size->setText(QStringLiteral("%1 × %2").arg(out.width()).arg(out.height()));
        m_size->setAccessibleName(tr("Output size: %1 by %2 pixels").arg(out.width()).arg(out.height()));
    }
    emit styleChanged(m_style);
}

}
