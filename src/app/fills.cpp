#include "app/fills.h"
#include "app/spring.h"
#include "app/theme.h"
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QToolButton>
#include <QStylePainter>
#include <QStyleOptionToolButton>
#include <QWidget>
#include <functional>
#include <optional>

namespace eddy {

static HoverFills *g_fills = nullptr;
// The theme in use, read where it is needed rather than mirrored into state.
static bool dark() { return qApp->palette().color(QPalette::Window).lightness() < 128; }

void HoverFills::install() {
    if (g_fills) return;
    g_fills = new HoverFills(qApp);
    qApp->installEventFilter(g_fills);
}

qreal HoverFills::level(const QToolButton *button) {
    return g_fills ? g_fills->m_levels.value(button) : 0;
}

qreal HoverFills::checkedLevel(const QToolButton *button) {
    return g_fills ? g_fills->m_checkLevels.value(button) : 0;
}

void easeToggle(QToolButton *button) {
    button->setProperty("easedToggle", true);   // eddy.qss leaves its checked fill off
    button->style()->unpolish(button);
    button->style()->polish(button);
}

namespace {
// Buttons with a fill at rest (the primary actions, the colour popover's two wide
// buttons): HoverFills paints that fill and eases it a step on hover, so the
// stylesheet leaves it off. The rest colour by name.
std::optional<QColor> restingFill(const QToolButton *button) {
    const QString name = button->objectName();
    if (name == QLatin1String("ExportSave") || name == QLatin1String("ToastAction")
        || name == QLatin1String("ResumeOpen"))
        return theme::tone(theme::Tone::Fg, dark());
    if (name == QLatin1String("CropApply"))   // @chip-on
        return theme::tone(dark() ? theme::Tone::Raise3 : theme::Tone::Fg, dark());
    if (name == QLatin1String("Custom") || name == QLatin1String("Pipette"))
        return theme::tone(theme::Tone::Raise2, dark());
    return std::nullopt;
}

// A segment of the export rows: its glide owns the fill.
bool ownsItsBackground(const QToolButton *button) {
    return button->objectName() == QLatin1String("ExportSegment");
}

// One step from rest: a light fill darkens, a dark one lightens, in either theme.
QColor hovered(const QColor &rest) {
    const QColor towards = rest.lightness() > 128 ? QColor(Qt::black) : QColor(Qt::white);
    constexpr qreal kStep = 0.12;
    return QColor::fromRgbF(rest.redF() + (towards.redF() - rest.redF()) * kStep,
                            rest.greenF() + (towards.greenF() - rest.greenF()) * kStep,
                            rest.blueF() + (towards.blueF() - rest.blueF()) * kStep);
}

// Icon buttons in the top bar and tool rail take the squircle mark their selection uses.
bool inChromeBar(const QToolButton *button) {
    for (const QWidget *w = button->parentWidget(); w; w = w->parentWidget())
        if (w->objectName() == QLatin1String("Toolbar") || w->objectName() == QLatin1String("ToolRail")) return true;
    return false;
}

// The marks of the top bar and tool rail: a squircle inset from the button, its
// ends as round as a square button's mark when the button is wider.
QPainterPath chromeMark(const QRectF &button) {
    const qreal inset = theme::kMarkInset;
    const QRectF mark = button.adjusted(inset, inset, -inset, -inset);
    return qFuzzyCompare(mark.width(), mark.height()) ? theme::squircle(mark)
                                                      : theme::squircleRect(mark, mark.height() / 2);
}

QPainterPath chip(const QRectF &button) {
    QPainterPath path;
    path.addRoundedRect(button, theme::kRChip, theme::kRChip);
    return path;
}

// A checked toggle: the step its surface uses for a choice.
QColor checkedFillFor(const QToolButton *button) {
    for (const QWidget *w = button->parentWidget(); w; w = w->parentWidget()) {
        const QString name = w->objectName();
        if (name == QLatin1String("Toolbar")) return theme::tone(theme::Tone::Raise2, dark());
        if (name == QLatin1String("PlaybackBar")) return theme::tone(theme::Tone::BarActive, dark());
    }
    return theme::tone(theme::Tone::Raise3, dark());
}

// One step above the surface the button sits on.
QColor fillFor(const QToolButton *button) {
    for (const QWidget *w = button->parentWidget(); w; w = w->parentWidget()) {
        const QString name = w->objectName();
        if (name == QLatin1String("Toolbar") || name == QLatin1String("ToolRail")
            || name == QLatin1String("ViewControls") || name == QLatin1String("PlaybackBar"))
            return theme::tone(theme::Tone::BarHover, dark());
        if (name == QLatin1String("ColorPopover")) return theme::tone(theme::Tone::Raise3, dark());
    }
    return theme::tone(theme::Tone::Raise2, dark());
}
}

Spring *HoverFills::checkSpringFor(QToolButton *button) {
    if (Spring *spring = m_checkSprings.value(button)) return spring;
    auto *spring = new Spring(motion::kFast, [this, button](qreal level) {
        m_checkLevels[button] = level;
        button->update();
    }, button);
    m_checkSprings.insert(button, spring);
    connect(button, &QObject::destroyed, this, [this, button] {
        m_checkSprings.remove(button);
        m_checkLevels.remove(button);
    });
    spring->jumpTo(button->isChecked() ? 1 : 0);   // the state it starts in shows at once
    return spring;
}

Spring *HoverFills::springFor(QToolButton *button) {
    if (Spring *spring = m_springs.value(button)) return spring;
    auto *spring = new Spring(motion::kFast, [this, button](qreal level) {
        m_levels[button] = level;
        button->update();
    }, button);
    m_springs.insert(button, spring);
    connect(button, &QObject::destroyed, this, [this, button] {
        m_springs.remove(button);
        m_levels.remove(button);
    });
    return spring;
}

// A checked choice shows its own fill (the glide, or the stylesheet's), never a hover on top.
static bool showsChoice(const QToolButton *button) {
    return button->isChecked() && !button->property("easedToggle").toBool();
}

void HoverFills::aim(QToolButton *button, bool hovered) {
    Spring *spring = springFor(button);
    const qreal target = hovered && button->isEnabled() && !showsChoice(button) ? 1 : 0;
    if (spring->target() != target) spring->setTarget(target);
}

namespace {
// QToolButton keeps its style option to itself; the paint below needs it.
struct OptionAccess : QToolButton {
    using QToolButton::initStyleOption;
};

// The button as the style draws it, at rest and then hovered on top at `level`: label
// and icon blend with the fill under them instead of switching when the pointer
// arrives. Padding, arrows and states stay the stylesheet's own.
void paintLabels(QToolButton *button, qreal level) {
    QStyleOptionToolButton option;
    static_cast<OptionAccess *>(button)->initStyleOption(&option);
    QStylePainter painter(button);
    QStyleOptionToolButton rest = option;
    rest.state &= ~QStyle::State_MouseOver;
    painter.drawComplexControl(QStyle::CC_ToolButton, rest);
    if (level <= 0.001) return;
    QStyleOptionToolButton hovered = option;
    hovered.state |= QStyle::State_MouseOver;
    painter.setOpacity(qMin<qreal>(1, level));
    painter.drawComplexControl(QStyle::CC_ToolButton, hovered);
}
}

bool HoverFills::eventFilter(QObject *watched, QEvent *event) {
    auto *button = qobject_cast<QToolButton *>(watched);
    if (!button) return false;
    // CapButton paints its own label and blends it with level(); the rest are painted here.
    const bool paintsItself = dynamic_cast<theme::CapButton *>(button) != nullptr;
    switch (event->type()) {
    case QEvent::Enter:
        aim(button, true);
        break;
    case QEvent::Leave:
    case QEvent::EnabledChange:
        aim(button, event->type() == QEvent::EnabledChange && button->underMouse());
        break;
    case QEvent::Hide:
        // A hidden button keeps no hover: it would show it on the next show.
        if (Spring *spring = m_springs.value(button)) spring->jumpTo(0);
        if (Spring *spring = m_checkSprings.value(button)) spring->jumpTo(button->isChecked() ? 1 : 0);
        break;
    case QEvent::Paint: {
        if (ownsItsBackground(button)) {   // the glide draws the fill; the label still blends
            if (paintsItself) break;
            paintLabels(button, qBound<qreal>(0, m_levels.value(button), 1));
            return true;
        }
        if (const std::optional<QColor> rest = restingFill(button)) {
            const qreal level = button->isEnabled() ? qBound<qreal>(0, m_levels.value(button), 1) : 0;
            if (level > 0.001 && !button->underMouse()) aim(button, false);
            // Disabled, a primary steps back to a plain field: its full fill read as clickable.
            const QColor base = button->isEnabled() ? *rest : theme::tone(theme::Tone::Raise2, dark());
            const QColor top = hovered(base);
            QPainter painter(button);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.fillPath(chip(QRectF(button->rect())),
                             QColor::fromRgbF(base.redF() + (top.redF() - base.redF()) * level,
                                              base.greenF() + (top.greenF() - base.greenF()) * level,
                                              base.blueF() + (top.blueF() - base.blueF()) * level));
            painter.end();
            if (paintsItself) break;
            paintLabels(button, level);
            return true;
        }
        const bool toggle = button->property("easedToggle").toBool();
        qreal checked = 0;
        if (toggle) {
            // A toggle repaints when it flips: the paint is where its fill turns.
            Spring *spring = checkSpringFor(button);
            const qreal target = button->isChecked() ? 1 : 0;
            if (spring->target() != target) spring->setTarget(target);
            checked = qBound<qreal>(0, m_checkLevels.value(button), 1);
        }
        // Chosen while hovered: the hover fades out as the glide arrives, it never cuts to nothing.
        if (showsChoice(button) && springFor(button)->target() != 0) aim(button, false);
        const qreal level = qBound<qreal>(0, m_levels.value(button), 1);
        // Wayland reports no Leave after a popup grab; a paint without the pointer ends it.
        if (level > 0.001 && !button->underMouse()) aim(button, false);
        if (level > 0.001 || checked > 0.001) {
            QPainter painter(button);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            const QRectF box(button->rect());
            const QPainterPath shape = inChromeBar(button) ? chromeMark(box) : chip(box);
            // The hover gives way as the checked fill comes in, so the two never stack.
            QColor hover = fillFor(button);
            hover.setAlphaF(hover.alphaF() * qMin<qreal>(1, level) * (1 - checked));
            if (hover.alphaF() > 0.001) painter.fillPath(shape, hover);
            QColor on = checkedFillFor(button);
            on.setAlphaF(on.alphaF() * checked);
            if (on.alphaF() > 0.001) painter.fillPath(shape, on);
        }
        if (paintsItself) break;
        paintLabels(button, level);
        return true;
    }
    default:
        break;
    }
    return false;
}

namespace {
// Paints the glide's fill; under the buttons, transparent to the pointer.
class GlideLayer : public QWidget {
public:
    GlideLayer(QWidget *surface, std::function<void(QPainter &)> paint)
        : QWidget(surface), m_paint(std::move(paint)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_paint(painter);
    }
private:
    std::function<void(QPainter &)> m_paint;
};
}

SelectionGlide::SelectionGlide(QButtonGroup *group, QWidget *surface, theme::Tone tone, Shape shape)
    : QObject(surface), m_group(group), m_surface(surface), m_tone(tone), m_shape(shape) {
    m_layer = new GlideLayer(surface, [this](QPainter &painter) {
        const QRectF rect = fillRect();
        if (rect.isEmpty() || m_a->value() <= 0.001) return;
        QColor fill = theme::tone(m_tone, dark());
        fill.setAlphaF(qMin<qreal>(1, m_a->value()));
        painter.fillPath(m_shape == Shape::Mark ? chromeMark(rect) : chip(rect), fill);
    });
    m_layer->lower();
    const auto repaint = [this](qreal) { m_layer->update(); };
    m_x = new Spring(motion::kFast, repaint, this);
    m_y = new Spring(motion::kFast, repaint, this);
    m_w = new Spring(motion::kFast, repaint, this);   // segments differ in width
    m_h = new Spring(motion::kFast, repaint, this);
    m_a = new Spring(motion::kFast, repaint, this);
    m_a->jumpTo(0);
    // Off as well as on: a mixed selection leaves nothing checked, and the fill goes.
    connect(group, &QButtonGroup::buttonToggled, this, [this] { follow(true); });
    surface->installEventFilter(this);
    for (QAbstractButton *button : group->buttons()) {
        button->installEventFilter(this);
        button->setProperty("glided", true);   // eddy.qss leaves their checked fill off
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    follow(false);
}

QRectF SelectionGlide::fillRect() const {
    if (!m_shown || !m_shown->isVisible()) return {};
    return QRectF(m_x->value(), m_y->value(), m_w->value(), m_h->value());
}

qreal SelectionGlide::opacity() const { return m_a->value(); }

QAbstractButton *SelectionGlide::checkedButton() const {
    for (QAbstractButton *button : m_group->buttons())
        if (button->isChecked()) return button;
    return nullptr;
}

void SelectionGlide::follow(bool animate) {
    animate = animate && m_layer->isVisible();
    QAbstractButton *checked = checkedButton();
    if (!checked) {
        // Keep the last place: the fill fades where it was.
        if (animate) m_a->setTarget(0); else m_a->jumpTo(0);
        return;
    }
    const QRectF at(checked->mapTo(m_surface, QPoint()), QSizeF(checked->size()));
    // From nothing, the fill appears in place rather than flying in from the old spot.
    const bool travel = animate && m_shown && m_a->value() > 0.05;
    m_shown = checked;
    for (auto [spring, value] : {std::pair{m_x, at.x()}, {m_y, at.y()}, {m_w, at.width()}, {m_h, at.height()}}) {
        if (travel) spring->setTarget(value); else spring->jumpTo(value);
    }
    if (animate) m_a->setTarget(1); else m_a->jumpTo(1);
}

bool SelectionGlide::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::Resize:
        if (watched == m_surface) m_layer->setGeometry(m_surface->rect());
        // A new layout is not a choice: no motion; but one mid-glide (a popover resizing
        // with its page) turns towards the new place instead of snapping there.
        follow(m_x->state() == QAbstractAnimation::Running || m_y->state() == QAbstractAnimation::Running);
        break;
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
        if (watched != m_surface || event->type() == QEvent::Show) follow(false);
        break;
    default:
        break;
    }
    return false;
}

namespace {
// The corner of a rounded square, drawn on its own: radius 3, a straight 4 px leg each way.
// The stroke's outer edge lies on the button's edge, so it stays clear of the mark kMarkInset in.
class ContextMark : public QWidget {
public:
    static constexpr qreal kRadius = 3, kLeg = 4, kPen = 1.5;
    static constexpr int kSize = int(kRadius + kLeg + kPen);   // 8: the arc, a leg and the pen
    explicit ContextMark(QToolButton *button) : QWidget(button) {
        setObjectName(QStringLiteral("ContextMark"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        resize(kSize, kSize);
        place();
        button->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize) place();
        return false;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                 QPalette::PlaceholderText), kPen);   // @sub, like a quiet label
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        const qreal end = kSize - kPen / 2;   // the stroke's centre line
        QPainterPath corner(QPointF(end, end - kRadius - kLeg));
        corner.lineTo(end, end - kRadius);
        corner.arcTo(QRectF(end - 2 * kRadius, end - 2 * kRadius, 2 * kRadius, 2 * kRadius), 0, -90);
        corner.lineTo(end - kRadius - kLeg, end);
        painter.drawPath(corner);
    }
private:
    void place() { move(parentWidget()->width() - kSize, parentWidget()->height() - kSize); raise(); }
};

class KeepOpen : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        auto *menu = static_cast<QMenu *>(watched);
        QAction *action = nullptr;
        if (event->type() == QEvent::MouseButtonRelease) {
            action = menu->actionAt(static_cast<QMouseEvent *>(event)->position().toPoint());
        } else if (event->type() == QEvent::KeyPress) {   // Enter or Space on the highlighted entry
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) action = menu->activeAction();
        } else {
            return false;
        }
        if (!action || !action->isCheckable() || !action->isEnabled()) return false;
        action->trigger();
        return true;
    }
};
}

bool fadeTarget(const QWidget *widget) {
    const QVariant target = widget->property("fadeTarget");
    return target.isValid() ? target.toBool() : widget->isVisible();
}

void fadeVisible(QWidget *widget, bool visible) {
    widget->setProperty("fadeTarget", visible);
    auto *spring = widget->findChild<Spring *>(QStringLiteral("FadeSpring"), Qt::FindDirectChildrenOnly);
    if (!spring) {
        auto *effect = new QGraphicsOpacityEffect(widget);
        effect->setEnabled(false);   // fully shown, the widget paints directly and stays crisp
        widget->setGraphicsEffect(effect);
        spring = new Spring(motion::kBase, [widget](qreal opacity) {
            // Looked up each frame: the widget owns the effect and may replace it.
            auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
            if (!effect) return;
            effect->setOpacity(opacity);
            effect->setEnabled(opacity < 0.999);
            if (opacity <= 0.001 && !widget->property("fadeTarget").toBool()) widget->hide();
        }, widget);
        spring->setObjectName(QStringLiteral("FadeSpring"));
        spring->jumpTo(widget->isVisible() ? 1 : 0);
    }
    // Fading out it is already gone for the pointer: clicks reach what is under it.
    widget->setAttribute(Qt::WA_TransparentForMouseEvents, !visible);
    if (visible && widget->isHidden()) {
        spring->jumpTo(0);
        widget->show();
    }
    if (!visible && widget->isHidden()) return;
    spring->setTarget(visible ? 1 : 0);
}

void markContextMenu(QToolButton *button) {
    if (!button->findChild<QWidget *>(QStringLiteral("ContextMark"), Qt::FindDirectChildrenOnly))
        new ContextMark(button);
}

void keepOpenOnToggle(QMenu *menu) {
    menu->installEventFilter(new KeepOpen(menu));
}

}
