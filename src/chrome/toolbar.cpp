#include "chrome/toolbar.h"
#include "app/fills.h"
#include <QWidgetAction>
#include "popovers/colorpopover.h"
#include "app/theme.h"
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
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <cmath>

namespace eddy {

QMenu *Toolbar::enableExportMenu(QWidget *panel) {
    auto *save = findChild<QToolButton *>(QStringLiteral("Save"));
    if (!save || save->menu()) return save ? save->menu() : nullptr;
    auto *menu = new QMenu(save);
    menu->setObjectName(QStringLiteral("ExportMenu"));
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(panel);
    menu->addAction(action);
    save->setMenu(menu);
    save->setPopupMode(QToolButton::DelayedPopup);
    save->setFocusPolicy(Qt::StrongFocus);
    save->setToolTip(tr("Save\tEnter\nExport options\tRight-click"));
    save->setAccessibleName(tr("Save; right-click or press Alt+Down for export options"));
    markContextMenu(save);
    return menu;
}

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
    copy->setToolTip(tr("Copy video\tCtrl+C\nCopy frame\tCtrl+Shift+C\nMore\tRight-click"));
    copy->setAccessibleName(tr("Copy video; right-click or press Alt+Down for frame copy"));
    markContextMenu(copy);
}

static QToolButton *mkBtn(bool checkable, bool square, QToolButton *b = nullptr) {
    if (!b) b = new QToolButton;
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);      // keep keyboard focus on the window for hotkeys
    b->setCursor(Qt::PointingHandCursor);
    if (square) b->setFixedSize(theme::kControl);
    else b->setFixedHeight(theme::kCtl);
    return b;
}

Toolbar::Toolbar(QWidget *parent) : QWidget(parent) {
    setObjectName("Toolbar");
    setAttribute(Qt::WA_StyledBackground, true);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(theme::kBarPad, theme::kBarPad, theme::kBarPad, theme::kBarPad);   // same left axis as the tool rail
    lay->setSpacing(theme::kS1);
    m_toolRail = new QWidget(this);
    m_toolRail->setObjectName(QStringLiteral("ToolRail"));
    m_toolRail->setAttribute(Qt::WA_StyledBackground, true);
    m_toolRail->hide(); // The editor places this beside the canvas.
    auto *rail = new QVBoxLayout(m_toolRail);
    rail->setContentsMargins(theme::kBarPad, theme::kBarPad, theme::kBarPad, theme::kBarPad);
    rail->setSpacing(theme::kS1);
    m_toolRail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *group = new QButtonGroup(this); group->setExclusive(true);
    const QPalette palette = QApplication::palette();
    const QColor iconRest = palette.color(QPalette::PlaceholderText);
    const QColor iconOn = palette.color(QPalette::WindowText);
    const QColor iconHover = palette.color(QPalette::WindowText);

    m_undoBtn = mkBtn(false, true); m_undoBtn->setObjectName("Undo");
    m_undoBtn->setIcon(theme::tintedIcon(":/icons/undo.svg", iconRest, iconHover));
    m_undoBtn->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_undoBtn->setToolTip("Undo\tCtrl+Z"); m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QToolButton::clicked, this, [this]{ emit undoRequested(); });
    lay->addWidget(m_undoBtn);

    m_redoBtn = mkBtn(false, true); m_redoBtn->setObjectName("Redo");
    m_redoBtn->setIcon(theme::tintedIcon(":/icons/redo.svg", iconRest, iconHover));
    m_redoBtn->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_redoBtn->setToolTip("Redo\tCtrl+Shift+Z"); m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QToolButton::clicked, this, [this]{ emit redoRequested(); });
    lay->addWidget(m_redoBtn);

    lay->addSpacing(theme::kS3);

    struct T { ToolType type; const char *id; const char *name; const char *key; };
    const QVector<T> tools = {
        {ToolType::Move,"move","Move","M"}, {ToolType::Arrow,"arrow","Arrow","A"},
        {ToolType::Pen,"pen","Pen","P"}, {ToolType::Rect,"rect","Rectangle","R"},
        {ToolType::Ellipse,"ellipse","Ellipse","E"}, {ToolType::Highlight,"highlight","Highlight","H"},
        {ToolType::Text,"text","Text","T"},
        {ToolType::Step,"step","Step","N"},
        {ToolType::Redact,"redact","Redact","X"},
        {ToolType::Spotlight,"spotlight","Spotlight",""},
        {ToolType::Crop,"crop","Crop","C"},
    };
    for (const T &t : tools) {
        auto *b = mkBtn(true, true);
        b->setIcon(theme::tintedIcon(QString(":/icons/%1.svg").arg(t.id),
                                     iconRest, iconOn));
        // The rail has nothing beside its glyphs, so they run a step larger than the top bar's.
        b->setIconSize(QSize(theme::kFloatIcon, theme::kFloatIcon));
        b->setObjectName(QString::fromLatin1(t.id));
        b->setToolTip(*t.key ? QString("%1\t%2").arg(t.name, t.key)
                             : QString::fromLatin1(t.name));
        b->setAccessibleName(QString::fromLatin1(t.name));
        group->addButton(b);
        m_btns.insert(int(t.type), b);
        connect(b, &QToolButton::clicked, this, [this, tt=t.type]{ emit toolChosen(tt); });
        rail->addWidget(b);
        // Groups by what a tool does: select | draw | label | hide | crop. Only space between them.
        if (t.type == ToolType::Move || t.type == ToolType::Highlight || t.type == ToolType::Step
            || t.type == ToolType::Spotlight)
            rail->addSpacing(theme::kS3);
    }
    new SelectionGlide(group, m_toolRail, theme::Tone::Raise2);

    auto *wgroup = new QButtonGroup(this); wgroup->setExclusive(true);
    struct W { const char *id; const char *icon; const char *tip; double w; };
    const QVector<W> widths = {
        {"WidthS", "width-thin", "Thin line\t2 px", 2.0},
        {"WidthM", "width-medium", "Medium line\t4 px", 4.0},
        {"WidthL", "width-thick", "Thick line\t8 px", 8.0},
    };
    for (const W &x : widths) {
        auto *b = mkBtn(true, true);
        b->setObjectName(x.id);
        b->setIcon(theme::tintedIcon(QString(":/icons/%1.svg").arg(x.icon),
                                     iconRest, iconHover));
        b->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
        b->setToolTip(QString::fromUtf8(x.tip));
        b->setAccessibleName(theme::spokenName(b->toolTip()));
        if (x.w == 4.0) b->setChecked(true);             // default M
        wgroup->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, w=x.w]{ emit widthChosen(w); });
        lay->addWidget(b);
    }
    new SelectionGlide(wgroup, this, theme::Tone::Raise2);

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
    lay->addSpacing(theme::kS3);

    // Studio frames the output, so it sits with the output actions. Its checked
    // state mirrors the document; a click always opens the popover.
    m_studioBtn = mkBtn(true, false, new theme::CapButton); m_studioBtn->setObjectName("Studio");
    m_studioBtn->setIcon(theme::tintedIcon(QStringLiteral(":/icons/studio.svg"), iconRest, iconHover));
    m_studioBtn->setIconSize(QSize(theme::kLabelIcon, theme::kLabelIcon));
    m_studioBtn->setText(QStringLiteral("Studio"));
    m_studioBtn->setToolTip(QStringLiteral("Studio background and framing"));
    m_studioBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_studioBtn, &QToolButton::clicked, this, [this] {
        m_studioBtn->setChecked(!m_studioBtn->isChecked());   // undo Qt's toggle
        emit studioRequested();
    });
    lay->addWidget(m_studioBtn);
    lay->addSpacing(theme::kS3);

    // Actions brighten on hover; the active tool keeps its own selection state.
    auto *save = mkBtn(false, true); save->setObjectName("Save");
    save->setIcon(theme::tintedIcon(QStringLiteral(":/icons/save.svg"),
                                    iconRest, iconHover));
    save->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    save->setToolTip("Save\tEnter");
    connect(save, &QToolButton::clicked, this, [this]{ emit saveRequested(); });
    lay->addWidget(save);

    auto *copy = mkBtn(false, true); copy->setObjectName("Copy");
    copy->setIcon(theme::tintedIcon(QStringLiteral(":/icons/copy.svg"),
                                    iconRest, iconHover));
    copy->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    copy->setToolTip("Copy to clipboard\tCtrl+C");
    connect(copy, &QToolButton::clicked, this, [this]{ emit copyRequested(); });
    lay->addWidget(copy);

    auto *shelf = mkBtn(false, false, new theme::CapButton); shelf->setObjectName("SendToShelf");
    // Same icon box as Save and Copy so the row's glyphs start and end on one
    // line; the label takes the body step so the 15px ink is not larger than it.
    shelf->setIcon(theme::tintedIcon(QStringLiteral(":/icons/shelf.svg"),
                                     iconRest, iconHover));
    shelf->setIconSize(QSize(theme::kLabelIcon, theme::kLabelIcon));
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
        if (button->accessibleName().isEmpty()) button->setAccessibleName(theme::spokenName(button->toolTip()));

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
    constexpr int d = theme::iconInk(theme::kIconSize);   // the size of its neighbours' ink
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

void Toolbar::setStudioActive(bool on) {
    m_studioBtn->setChecked(on);
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
        {"Save", "save"}, {"Copy", "copy"}, {"SendToShelf", "shelf"}, {"Studio", "studio"},
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
    m_themeBtn->setAccessibleName(theme::spokenName(m_themeBtn->toolTip()));
    setSwatchColor(m_swatchColor);
}

}
