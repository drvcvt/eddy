#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include "app/theme.h"

class QAbstractButton;
class QButtonGroup;
class QMenu;
class QToolButton;
class QWidget;

namespace eddy {

class Spring;

// Hover fills for the tool buttons, eased in and out with motion::kFast. The
// stylesheet leaves their hover background off; this paints it from the button's
// own Paint event, before the button draws, so label and icon stay on top.
// Buttons with a background of their own (primary actions, swatches) keep theirs.
class HoverFills : public QObject {
    Q_OBJECT
public:
    // One instance for the application, filtering every widget.
    static void install();
    // The fill shown on a button right now, 0 to 1; for tests.
    static qreal level(const QToolButton *button);
    // How far an eased toggle shows its checked fill, 0 to 1; for tests.
    static qreal checkedLevel(const QToolButton *button);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit HoverFills(QObject *parent) : QObject(parent) {}
    Spring *springFor(QToolButton *button);
    Spring *checkSpringFor(QToolButton *button);
    void aim(QToolButton *button, bool hovered);
    QHash<const QToolButton *, Spring *> m_springs, m_checkSprings;
    QHash<const QToolButton *, qreal> m_levels, m_checkLevels;
};

// A toggle (Bold, Studio, Loop) whose checked fill eases in and out with
// motion::kFast; HoverFills paints it, the stylesheet leaves it off.
void easeToggle(QToolButton *button);

// The checked button of an exclusive group shows one fill that glides to the
// next choice (motion::kFast), instead of one fill going out and another coming on.
// It paints from a layer under the buttons, so their own checked fill stays off.
// Shows or hides `widget` through its opacity on motion::kBase, in place: it keeps its
// slot, so nothing beside it moves. Hidden once faded out; with motion off, at once.
// While it fades out it takes no mouse input. A child widget only: Qt applies no
// effect to a window, and Wayland gives windows no opacity.
void fadeVisible(QWidget *widget, bool visible);
// Where a fadeVisible widget is going: shown or not, already while it fades.
bool fadeTarget(const QWidget *widget);
// Checkable entries switch without closing the menu, so a setting can be flipped
// and the next one reached; plain entries still run and close it.
void keepOpenOnToggle(QMenu *menu);

class SelectionGlide : public QObject {
    Q_OBJECT
public:
    // Mark: the inset squircle of the top bar and tool rail. Chip: the button's own
    // rounded box, for the floating bars, popovers and segmented rows.
    enum class Shape { Mark, Chip };
    SelectionGlide(QButtonGroup *group, QWidget *surface, theme::Tone tone, Shape shape = Shape::Mark);
    QRectF fillRect() const;   // where the fill shows now, in surface coordinates; for tests
    qreal opacity() const;     // 0 while nothing is chosen (a mixed selection); for tests

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void follow(bool animate);
    QAbstractButton *checkedButton() const;
    QButtonGroup *m_group;
    QWidget *m_surface;
    QWidget *m_layer;
    theme::Tone m_tone;
    Shape m_shape;
    Spring *m_x, *m_y, *m_w, *m_h, *m_a;
    QPointer<QAbstractButton> m_shown;
};

}
