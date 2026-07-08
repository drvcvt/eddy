#include "toolbar.h"
#include "colorpopover.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QFrame>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QPen>

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
    m_pill->resize(30, 30);   // initial only — movePillTo drives geometry (don't fix the size, or the inset can't shrink it)
    m_pill->hide();
    m_pillAnim = new QPropertyAnimation(m_pill, "geometry", this);
    m_pillAnim->setDuration(190);
    m_pillAnim->setEasingCurve(QEasingCurve::OutExpo);   // smooth glide, no overshoot

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(4);
    auto *group = new QButtonGroup(this); group->setExclusive(true);

    m_undoBtn = mkBtn(false, true); m_undoBtn->setObjectName("Undo");
    m_undoBtn->setText(QString::fromUtf8("\xE2\x86\xB6"));   // ↶
    m_undoBtn->setToolTip("Undo \xC2\xB7 Ctrl+Z"); m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QToolButton::clicked, this, [this]{ emit undoRequested(); });
    lay->addWidget(m_undoBtn);

    m_redoBtn = mkBtn(false, true); m_redoBtn->setObjectName("Redo");
    m_redoBtn->setText(QString::fromUtf8("\xE2\x86\xB7"));   // ↷
    m_redoBtn->setToolTip("Redo \xC2\xB7 Ctrl+Shift+Z"); m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QToolButton::clicked, this, [this]{ emit redoRequested(); });
    lay->addWidget(m_redoBtn);

    auto *usep = new QFrame; usep->setObjectName("Sep");
    usep->setFrameShape(QFrame::VLine); usep->setFixedHeight(20);
    lay->addWidget(usep);

    struct T { ToolType type; const char *id; const char *name; const char *key; };
    const QVector<T> tools = {
        {ToolType::Move,"move","Move","M"}, {ToolType::Arrow,"arrow","Arrow","A"},
        {ToolType::Pen,"pen","Pen","P"}, {ToolType::Rect,"rect","Rectangle","R"},
        {ToolType::Ellipse,"ellipse","Ellipse","E"}, {ToolType::Highlight,"highlight","Highlight","H"},
        {ToolType::Text,"text","Text","T"},
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

    auto *wsep = new QFrame; wsep->setObjectName("Sep");
    wsep->setFrameShape(QFrame::VLine); wsep->setFixedHeight(20);
    lay->addWidget(wsep);

    auto *wgroup = new QButtonGroup(this); wgroup->setExclusive(true);
    struct W { const char *id; const char *label; double w; };
    const QVector<W> widths = { {"WidthS","S",2.0}, {"WidthM","M",4.0}, {"WidthL","L",8.0} };
    for (const W &x : widths) {
        auto *b = mkBtn(true, true);
        b->setObjectName(x.id);
        b->setText(x.label);
        b->setToolTip(QString("Line width %1").arg(x.label));
        if (x.w == 4.0) b->setChecked(true);             // default M
        wgroup->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, w=x.w]{ emit widthChosen(w); });
        lay->addWidget(b);
    }

    lay->addStretch(1);

    auto *color = mkBtn(false, true);
    color->setObjectName("Swatch");
    color->setToolTip("Stroke colour");
    m_swatch = color;
    setSwatchColor(QColor(theme::kStroke));        // painted disc; overridden by the real stroke colour
    connect(color, &QToolButton::clicked, this, [this, color]{
        auto *pop = new ColorPopover(this);
        pop->setAttribute(Qt::WA_DeleteOnClose);   // don't accumulate popovers on repeated opens
        connect(pop, &ColorPopover::picked, this, [this](const QColor &c){
            setSwatchColor(c);                     // tint the disc to the chosen colour
            emit colorChosen(c);
        });
        connect(pop, &ColorPopover::eyedropperRequested, this, &Toolbar::eyedropperRequested);
        pop->adjustSize();
        pop->move(color->mapToGlobal(QPoint(0, color->height() + 4)));
        pop->show();
    });
    lay->addWidget(color);

    auto *sep = new QFrame; sep->setObjectName("Sep");
    sep->setFrameShape(QFrame::VLine); sep->setFixedHeight(20);
    lay->addWidget(sep);

    // Save/Copy are not checkable (no sliding pill), so hover brightens to white
    // rather than kIconActive (which is dark, meant for the light pill behind tools).
    auto *save = mkBtn(false, false); save->setObjectName("Save");
    save->setIcon(theme::tintedIcon(QStringLiteral(":/icons/save.svg"),
                                    QColor(theme::kIconRest), QColor("#ffffff")));
    save->setIconSize(QSize(20, 20));
    save->setToolTip("Save \xC2\xB7 Enter");
    connect(save, &QToolButton::clicked, this, [this]{ emit saveRequested(); });
    lay->addWidget(save);

    auto *copy = mkBtn(false, false); copy->setObjectName("Copy");
    copy->setIcon(theme::tintedIcon(QStringLiteral(":/icons/copy.svg"),
                                    QColor(theme::kIconRest), QColor("#ffffff")));
    copy->setIconSize(QSize(20, 20));
    copy->setToolTip("Copy to clipboard \xC2\xB7 Ctrl+C");
    connect(copy, &QToolButton::clicked, this, [this]{ emit copyRequested(); });
    lay->addWidget(copy);

    auto *shelf = mkBtn(false, false); shelf->setObjectName("SendToShelf");
    shelf->setIcon(theme::tintedIcon(QStringLiteral(":/icons/shelf.svg"),
                                     QColor(theme::kIconRest), QColor("#ffffff")));
    shelf->setIconSize(QSize(20, 20));
    shelf->setToolTip("Send to Boltsnap shelf");
    connect(shelf, &QToolButton::clicked, this, [this]{ emit sendToShelfRequested(); });
    lay->addWidget(shelf);

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
    const QRect target = b->geometry().adjusted(2, 2, -2, -2);  // inset → contained squircle chip
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

void Toolbar::setUndoEnabled(bool on) { if (m_undoBtn) m_undoBtn->setEnabled(on); }
void Toolbar::setRedoEnabled(bool on) { if (m_redoBtn) m_redoBtn->setEnabled(on); }

// Paint a crisp colour disc as the swatch icon: a filled circle in the current
// stroke colour with a subtle dark ring for contrast on the dark toolbar.
void Toolbar::setSwatchColor(const QColor &c) {
    if (!m_swatch) return;
    constexpr int d = 18;
    const qreal dpr = m_swatch->devicePixelRatioF();   // crisp on HiDPI, like tintedIcon
    QPixmap pm(qRound(d * dpr), qRound(d * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(c);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1));
    p.drawEllipse(QRectF(1, 1, d - 2, d - 2));         // logical coords; painter is DPR-scaled
    p.end();
    m_swatch->setIcon(QIcon(pm));
    m_swatch->setIconSize(QSize(d, d));
}

}
