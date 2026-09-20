#include "toolbar.h"
#include "colorpopover.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QPen>
#include <QApplication>
#include <QColorDialog>
#include <QTimer>
#include <QMenu>

namespace eddy {

void Toolbar::enableVideoFrameCopy() {
    auto *copy = findChild<QToolButton *>(QStringLiteral("Copy"));
    if (!copy || copy->menu()) return;
    auto *menu = new QMenu(copy);
    auto *video = menu->addAction(tr("Copy video"));
    video->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_C));
    connect(video, &QAction::triggered, this, &Toolbar::copyRequested);
    auto *frame = menu->addAction(tr("Copy frame"));
    frame->setObjectName(QStringLiteral("CopyVideoFrame"));
    frame->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    connect(frame, &QAction::triggered, this, &Toolbar::copyFrameRequested);
    copy->setMenu(menu);
    copy->setPopupMode(QToolButton::DelayedPopup);
    copy->setFocusPolicy(Qt::StrongFocus);
    copy->setToolTip(tr("Copy video · Ctrl+C\nCopy frame · Ctrl+Shift+C · hold for menu"));
    copy->setAccessibleName(tr("Copy video; hold or press Alt+Down for frame copy"));
}

static QToolButton *mkBtn(bool checkable, bool square) {
    auto *b = new QToolButton;
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);      // keep keyboard focus on the window for hotkeys
    b->setCursor(Qt::PointingHandCursor);
    if (square) b->setFixedSize(theme::kBarButton);
    else b->setFixedHeight(theme::kBarButton.height());
    return b;
}

Toolbar::Toolbar(QWidget *parent) : QWidget(parent) {
    setObjectName("Toolbar");
    setAttribute(Qt::WA_StyledBackground, true);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 3, 6, 3);
    lay->setSpacing(2);
    m_toolRail = new QWidget(this);
    m_toolRail->setObjectName(QStringLiteral("ToolRail"));
    m_toolRail->setAttribute(Qt::WA_StyledBackground, true);
    m_toolRail->hide(); // The editor places this beside the canvas.
    auto *rail = new QVBoxLayout(m_toolRail);
    rail->setContentsMargins(4, 4, 4, 4);
    rail->setSpacing(2);
    m_toolRail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *group = new QButtonGroup(this); group->setExclusive(true);
    const QPalette palette = QApplication::palette();
    const QColor iconRest = palette.color(QPalette::PlaceholderText);
    const QColor iconOn = palette.color(QPalette::WindowText);
    const QColor iconHover = palette.color(QPalette::WindowText);

    m_undoBtn = mkBtn(false, true); m_undoBtn->setObjectName("Undo");
    m_undoBtn->setIcon(theme::tintedIcon(":/icons/undo.svg", iconRest, iconHover));
    m_undoBtn->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_undoBtn->setToolTip("Undo \xC2\xB7 Ctrl+Z"); m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QToolButton::clicked, this, [this]{ emit undoRequested(); });
    lay->addWidget(m_undoBtn);

    m_redoBtn = mkBtn(false, true); m_redoBtn->setObjectName("Redo");
    m_redoBtn->setIcon(theme::tintedIcon(":/icons/redo.svg", iconRest, iconHover));
    m_redoBtn->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_redoBtn->setToolTip("Redo \xC2\xB7 Ctrl+Shift+Z"); m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QToolButton::clicked, this, [this]{ emit redoRequested(); });
    lay->addWidget(m_redoBtn);

    lay->addSpacing(6);

    struct T { ToolType type; const char *id; const char *name; const char *key; };
    const QVector<T> tools = {
        {ToolType::Move,"move","Move","M"}, {ToolType::Arrow,"arrow","Arrow","A"},
        {ToolType::Pen,"pen","Pen","P"}, {ToolType::Rect,"rect","Rectangle","R"},
        {ToolType::Ellipse,"ellipse","Ellipse","E"}, {ToolType::Highlight,"highlight","Highlight","H"},
        {ToolType::Text,"text","Text","T"},
        {ToolType::Redact,"redact","Redact","X"},
        {ToolType::Spotlight,"spotlight","Spotlight",""},
    };
    for (const T &t : tools) {
        auto *b = mkBtn(true, true);
        b->setIcon(theme::tintedIcon(QString(":/icons/%1.svg").arg(t.id),
                                     iconRest, iconOn));
        b->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
        b->setObjectName(QString::fromLatin1(t.id));
        b->setToolTip(*t.key ? QString("%1 \xC2\xB7 %2").arg(t.name, t.key)
                             : QString::fromLatin1(t.name));
        group->addButton(b);
        m_btns.insert(int(t.type), b);
        connect(b, &QToolButton::clicked, this, [this, tt=t.type]{ emit toolChosen(tt); });
        rail->addWidget(b);
    }

    auto *wgroup = new QButtonGroup(this); wgroup->setExclusive(true);
    struct W { const char *id; const char *icon; const char *tip; double w; };
    const QVector<W> widths = {
        {"WidthS", "width-thin", "Thin line · 2 px", 2.0},
        {"WidthM", "width-medium", "Medium line · 4 px", 4.0},
        {"WidthL", "width-thick", "Thick line · 8 px", 8.0},
    };
    for (const W &x : widths) {
        auto *b = mkBtn(true, true);
        b->setObjectName(x.id);
        b->setIcon(theme::tintedIcon(QString(":/icons/%1.svg").arg(x.icon),
                                     iconRest, iconHover));
        b->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
        b->setToolTip(QString::fromUtf8(x.tip));
        b->setAccessibleName(b->toolTip());
        if (x.w == 4.0) b->setChecked(true);             // default M
        wgroup->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, w=x.w]{ emit widthChosen(w); });
        lay->addWidget(b);
    }

    auto *color = mkBtn(false, true);
    color->setObjectName("Swatch");
    color->setToolTip("Stroke colour");
    m_swatch = color;
    setSwatchColor(QColor(theme::kStroke));        // painted disc; overridden by the real stroke colour
    connect(color, &QToolButton::clicked, this, [this, color]{
        auto *pop = new ColorPopover(this, m_swatchColor);
        pop->setAttribute(Qt::WA_DeleteOnClose);   // don't accumulate popovers on repeated opens
        connect(pop, &ColorPopover::picked, this, [this](const QColor &c){
            setSwatchColor(c);                     // tint the disc to the chosen colour
            emit colorChosen(c);
        });
        connect(pop, &ColorPopover::eyedropperRequested, this, &Toolbar::eyedropperRequested);
        connect(pop, &ColorPopover::customRequested, this, [this](const QColor &current) {
            // Let the popup close before the native colour dialog takes focus.
            QTimer::singleShot(0, this, [this, current] {
                const QColor c = QColorDialog::getColor(current, this);
                if (!c.isValid()) return;
                setSwatchColor(c);
                emit colorChosen(c);
            });
        });
        pop->adjustSize();
        pop->move(color->mapToGlobal(QPoint(0, color->height() + 4)));
        pop->show();
    });
    lay->addWidget(color);
    lay->addStretch(1);
    lay->addSpacing(6);

    // Actions brighten on hover; the active tool keeps its own selection state.
    auto *save = mkBtn(false, true); save->setObjectName("Save");
    save->setIcon(theme::tintedIcon(QStringLiteral(":/icons/save.svg"),
                                    iconRest, iconHover));
    save->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    save->setToolTip("Save \xC2\xB7 Enter");
    connect(save, &QToolButton::clicked, this, [this]{ emit saveRequested(); });
    lay->addWidget(save);

    auto *copy = mkBtn(false, true); copy->setObjectName("Copy");
    copy->setIcon(theme::tintedIcon(QStringLiteral(":/icons/copy.svg"),
                                    iconRest, iconHover));
    copy->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    copy->setToolTip("Copy to clipboard \xC2\xB7 Ctrl+C");
    connect(copy, &QToolButton::clicked, this, [this]{ emit copyRequested(); });
    lay->addWidget(copy);

    auto *shelf = mkBtn(false, false); shelf->setObjectName("SendToShelf");
    shelf->setIcon(theme::tintedIcon(QStringLiteral(":/icons/shelf.svg"),
                                     iconRest, iconHover));
    shelf->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    shelf->setToolTip("Send to Boltsnap shelf");
    shelf->setText(QStringLiteral("To shelf"));
    shelf->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(shelf, &QToolButton::clicked, this, [this]{ emit sendToShelfRequested(); });
    lay->addWidget(shelf);

    m_themeBtn = mkBtn(false, true);
    m_themeBtn->setObjectName(QStringLiteral("Theme"));
    m_themeBtn->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    connect(m_themeBtn, &QToolButton::clicked, this, &Toolbar::themeToggleRequested);
    lay->addWidget(m_themeBtn);

    for (auto *button : findChildren<QToolButton *>())
        if (button->accessibleName().isEmpty()) button->setAccessibleName(button->toolTip());

    syncTool(ToolType::Arrow);   // sensible default highlight (snaps on first show)
    setDark(QApplication::palette().color(QPalette::Window).lightness() < 128);
}

void Toolbar::syncTool(ToolType t) {
    auto *b = m_btns.value(int(t), nullptr);
    if (!b) return;
    b->setChecked(true);                 // exclusive group unchecks the rest
}

void Toolbar::setUndoEnabled(bool on) { if (m_undoBtn) m_undoBtn->setEnabled(on); }
void Toolbar::setRedoEnabled(bool on) { if (m_redoBtn) m_redoBtn->setEnabled(on); }

// Paint a crisp colour disc as the swatch icon: a filled circle in the current
// stroke colour with a subtle dark ring for contrast on the dark toolbar.
void Toolbar::setSwatchColor(const QColor &c) {
    if (!m_swatch) return;
    m_swatchColor = c;
    constexpr int d = 14;
    const qreal dpr = m_swatch->devicePixelRatioF();   // crisp on HiDPI, like tintedIcon
    QPixmap pm(qRound(d * dpr), qRound(d * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(c);
    QColor ring = QApplication::palette().color(QPalette::WindowText);
    ring.setAlpha(90);
    p.setPen(QPen(ring, 1));
    p.drawEllipse(QRectF(1, 1, d - 2, d - 2));         // logical coords; painter is DPR-scaled
    p.end();
    m_swatch->setIcon(QIcon(pm));
    m_swatch->setIconSize(QSize(d, d));
}

void Toolbar::setDark(bool dark) {
    const QPalette palette = QApplication::palette();
    const QColor rest = palette.color(QPalette::PlaceholderText);
    const QColor active = palette.color(QPalette::WindowText);
    const QColor hover = palette.color(QPalette::WindowText);
    for (QToolButton *button : m_btns)
        button->setIcon(theme::tintedIcon(
            QStringLiteral(":/icons/%1.svg").arg(button->objectName()), rest, active));
    const struct { const char *name; const char *icon; } actions[] = {
        {"Undo", "undo"}, {"Redo", "redo"},
        {"Save", "save"}, {"Copy", "copy"}, {"SendToShelf", "shelf"},
        {"WidthS", "width-thin"}, {"WidthM", "width-medium"}, {"WidthL", "width-thick"}};
    for (const auto &action : actions)
        if (auto *button = findChild<QToolButton *>(QString::fromLatin1(action.name)))
            button->setIcon(theme::tintedIcon(
                QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(action.icon)), rest, hover));
    m_themeBtn->setText({});
    m_themeBtn->setIcon(theme::tintedIcon(
        dark ? QStringLiteral(":/icons/sun.svg") : QStringLiteral(":/icons/moon.svg"),
        rest, hover));
    m_themeBtn->setToolTip(dark ? QStringLiteral("Switch to light theme")
                                : QStringLiteral("Switch to dark theme"));
    m_themeBtn->setAccessibleName(m_themeBtn->toolTip());
    setSwatchColor(m_swatchColor);
}

}
