#include "app/fills.h"
#include "app/spring.h"
#include "app/theme.h"
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QPainter>
#include <QToolButton>
#include <QWidget>
#include <functional>

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

namespace {
// Buttons drawn on their own fill: the primary actions and swatches.
bool ownsItsBackground(const QToolButton *button) {
    static const QStringList names{"Custom", "Pipette", "ToastAction", "ExportSave", "ResumeOpen",
                                   "CropApply", "ExportSegment"};
    return names.contains(button->objectName());
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

void HoverFills::aim(QToolButton *button, bool hovered) {
    Spring *spring = springFor(button);
    const qreal target = hovered && button->isEnabled() ? 1 : 0;
    if (spring->target() != target) spring->setTarget(target);
}

bool HoverFills::eventFilter(QObject *watched, QEvent *event) {
    auto *button = qobject_cast<QToolButton *>(watched);
    if (!button || ownsItsBackground(button)) return false;
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
        break;
    case QEvent::Paint: {
        const qreal level = m_levels.value(button);
        if (level <= 0.001 || button->isChecked()) break;
        // Wayland reports no Leave after a popup grab; a paint without the pointer ends it.
        if (!button->underMouse()) aim(button, false);
        QPainter painter(button);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor fill = fillFor(button);
        fill.setAlphaF(fill.alphaF() * qMin<qreal>(1, level));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(button->rect()), theme::kRChip, theme::kRChip);
        break;
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

SelectionGlide::SelectionGlide(QButtonGroup *group, QWidget *surface, theme::Tone tone)
    : QObject(surface), m_group(group), m_surface(surface), m_tone(tone) {
    m_layer = new GlideLayer(surface, [this](QPainter &painter) {
        const QRectF rect = fillRect();
        if (rect.isEmpty()) return;
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::tone(m_tone, dark()));
        painter.drawRoundedRect(rect, theme::kRChip, theme::kRChip);
    });
    m_layer->lower();
    m_x = new Spring(motion::kFast, [this](qreal) { m_layer->update(); }, this);
    m_y = new Spring(motion::kFast, [this](qreal) { m_layer->update(); }, this);
    connect(group, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool checked) {
        if (checked) follow(true);
    });
    surface->installEventFilter(this);
    for (QAbstractButton *button : group->buttons()) button->installEventFilter(this);
}

QRectF SelectionGlide::fillRect() const {
    if (!m_shown || !m_shown->isVisible()) return {};
    return QRectF(QPointF(m_x->value(), m_y->value()), QSizeF(m_shown->size()));
}

void SelectionGlide::follow(bool animate) {
    QAbstractButton *checked = m_group->checkedButton();
    m_shown = checked;
    if (!checked) { m_layer->update(); return; }
    const QPointF at = checked->mapTo(m_surface, QPoint());
    if (animate && m_layer->isVisible()) {
        m_x->setTarget(at.x());
        m_y->setTarget(at.y());
    } else {
        m_x->jumpTo(at.x());
        m_y->jumpTo(at.y());
    }
}

bool SelectionGlide::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::Resize:
        if (watched == m_surface) m_layer->setGeometry(m_surface->rect());
        follow(false);   // a new layout is not a choice: no motion
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

}
