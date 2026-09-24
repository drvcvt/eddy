#include "studiopopover.h"
#include "motionicon.h"
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
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

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

StudioPopover::StudioPopover(const StudioStyle &style, QSize content, const StudioCameraSettings &camera,
                             QWidget *parent)
    : QWidget(parent, Qt::Popup), m_style(style), m_content(content) {
    setObjectName(QStringLiteral("StudioPopover"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(12);
    auto *stylePage = new QWidget(this);
    auto *grid = new QGridLayout(stylePage);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(6);
    int row = 0;
    auto section = [&](const QString &text) {
        auto *label = new QLabel(text.toUpper(), stylePage);
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
        auto *b = new QToolButton(stylePage);
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
    auto *image = new QToolButton(stylePage);
    image->setObjectName(QStringLiteral("StudioImage"));
    image->setText(tr("Image…"));
    image->setToolTip(tr("Use an image as background"));
    image->setFixedHeight(theme::kFloatButton.height());
    image->setCursor(Qt::PointingHandCursor);
    connect(image, &QToolButton::clicked, this, &StudioPopover::imageRequested);
    grid->addWidget(image, row++, choices.size());

    auto slider = [&](const QString &text, double value, double max, auto setter) {
        auto *label = new QLabel(text, stylePage);
        label->setObjectName(QStringLiteral("StudioLabel"));
        grid->addWidget(label, row, 0, 1, 3);
        auto *s = new QSlider(Qt::Horizontal, stylePage);
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
        auto *b = new QToolButton(stylePage);
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
    m_size = new QLabel(stylePage);
    m_size->setObjectName(QStringLiteral("StudioSize"));
    m_size->setToolTip(tr("Output size in pixels"));
    ratioRow->addWidget(m_size);
    grid->addLayout(ratioRow, row++, 0, 1, 8);
    outer->addWidget(stylePage);
    // The header: Style and Camera for videos, Presets on the right always.
    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(4);
    outer->insertLayout(0, tabs);
    m_presets = new QToolButton(this);
    m_presets->setObjectName(QStringLiteral("StudioPresets"));
    m_presets->setText(tr("Presets"));
    m_presets->setToolTip(tr("Saved styles to apply, save, import or share"));
    m_presets->setFixedHeight(theme::kFloatButton.height());
    m_presets->setCursor(Qt::PointingHandCursor);
    m_presets->setPopupMode(QToolButton::InstantPopup);
    auto *presetMenu = new QMenu(m_presets);
    presetMenu->setObjectName(QStringLiteral("StudioPresetMenu"));
    presetMenu->setWindowFlag(Qt::FramelessWindowHint);
    presetMenu->setAttribute(Qt::WA_TranslucentBackground);
    m_presets->setMenu(presetMenu);
    setPresets({});
    if (camera.available) {
        auto *tabGroup = new QButtonGroup(this);
        const QStringList names{tr("Style"), tr("Camera")};
        for (int i = 0; i < names.size(); ++i) {
            auto *tab = new QToolButton(this);
            tab->setObjectName(QStringLiteral("StudioPage"));
            tab->setText(names[i]);
            tab->setCheckable(true);
            tab->setChecked(i == 0);
            tab->setFixedHeight(theme::kFloatButton.height());
            tab->setCursor(Qt::PointingHandCursor);
            tabGroup->addButton(tab, i);
            tabs->addWidget(tab);
        }
        tabs->addStretch(1);

        auto *cameraPage = new QWidget(this);
        auto *rows = new QGridLayout(cameraPage);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setHorizontalSpacing(4);
        rows->setVerticalSpacing(6);
        auto rowLabel = [&](const QString &text, int row) {
            auto *label = new QLabel(text, cameraPage);
            label->setObjectName(QStringLiteral("StudioLabel"));
            rows->addWidget(label, row, 0);
        };
        rowLabel(tr("Motion"), 0);
        auto *motions = new QButtonGroup(this);
        const QColor ink = QApplication::palette().color(QPalette::WindowText);
        int column = 1;
        for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                            ZoomSegment::Motion::Instant}) {
            auto *b = new QToolButton(cameraPage);
            b->setObjectName(QStringLiteral("StudioMotion"));
            b->setText(motionName(motion));
            b->setIcon(motionIcon(motion, ink, 16));
            b->setIconSize(QSize(16, 16));
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setCheckable(true);
            b->setChecked(camera.motion == motion);
            b->setFixedHeight(theme::kFloatButton.height());
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(tr("Every zoom moves like this; new zooms too"));
            motions->addButton(b);
            rows->addWidget(b, 0, column++);
            connect(b, &QToolButton::clicked, this, [this, motion] { emit motionChosen(motion); });
        }
        // Mixed motions: nothing is checked until the user picks one.
        motions->setExclusive(camera.motion.has_value());
        connect(motions, &QButtonGroup::buttonClicked, this, [motions] { motions->setExclusive(true); });
        rowLabel(tr("Zoom"), 1);
        m_keep = new QToolButton(cameraPage);
        m_keep->setObjectName(QStringLiteral("StudioKeepZoomed"));
        m_keep->setText(tr("Keep zoomed in"));
        m_keep->setCheckable(true);
        m_keep->setChecked(camera.keepZoomedIn);
        m_keep->setFixedHeight(theme::kFloatButton.height());
        m_keep->setCursor(Qt::PointingHandCursor);
        rows->addWidget(m_keep, 1, 1, 1, 3, Qt::AlignLeft);
        connect(m_keep, &QToolButton::toggled, this, &StudioPopover::keepZoomedInChanged);
        if (camera.suggestAvailable) {
            auto *suggest = new QToolButton(cameraPage);
            suggest->setObjectName(QStringLiteral("StudioSuggest"));
            suggest->setText(tr("Suggest zooms"));
            suggest->setToolTip(tr("Add zooms where the pointer clicks or rests"));
            suggest->setFixedHeight(theme::kFloatButton.height());
            suggest->setCursor(Qt::PointingHandCursor);
            rows->addWidget(suggest, 2, 1, 1, 3, Qt::AlignLeft);
            connect(suggest, &QToolButton::clicked, this, &StudioPopover::suggestRequested);
        }
        setKeepZoomedInAvailable(camera.keepZoomedInAvailable);
        rows->setRowStretch(3, 1);
        outer->addWidget(cameraPage);
        cameraPage->hide();
        // Each page takes its own size; the right edge stays under the Studio button.
        connect(tabGroup, &QButtonGroup::idClicked, this, [this, stylePage, cameraPage](int page) {
            const int right = geometry().right();
            stylePage->setVisible(page == 0);
            cameraPage->setVisible(page == 1);
            layout()->activate();
            setFixedSize(sizeHint());
            move(right - width() + 1, y());
        });
    }
    if (!camera.available) tabs->addStretch(1);
    tabs->addWidget(m_presets);
    apply();
    setFixedSize(sizeHint());
}

void StudioPopover::setPresets(const QStringList &names) {
    QMenu *menu = m_presets->menu();
    menu->clear();
    for (int i = 0; i < names.size(); ++i)
        connect(menu->addAction(names[i]), &QAction::triggered, this, [this, i] { emit presetChosen(i); });
    if (!names.isEmpty()) menu->addSeparator();
    connect(menu->addAction(tr("Save current…")), &QAction::triggered, this, &StudioPopover::presetSaveRequested);
    connect(menu->addAction(tr("Import…")), &QAction::triggered, this, &StudioPopover::presetImportRequested);
    connect(menu->addAction(tr("Export…")), &QAction::triggered, this, &StudioPopover::presetExportRequested);
}

void StudioPopover::setKeepZoomedInAvailable(bool available) {
    if (!m_keep) return;
    m_keep->setEnabled(available);
    m_keep->setToolTip(available ? tr("Fill the frame's ratio with a window of the video")
                                 : tr("Needs an output ratio that differs from the video"));
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
    showSize();
    emit styleChanged(m_style);
}

void StudioPopover::showSize() {
    if (!m_size) return;
    const QSize out = studioLayout(m_content, m_style).output;
    m_size->setText(QStringLiteral("%1 × %2").arg(out.width()).arg(out.height()));
    m_size->setAccessibleName(tr("Output size: %1 by %2 pixels").arg(out.width()).arg(out.height()));
}

void StudioPopover::setContentSize(QSize content) {
    m_content = content;
    showSize();
}

}
