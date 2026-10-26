#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include "app/theme.h"

class QAbstractButton;
class QButtonGroup;
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit HoverFills(QObject *parent) : QObject(parent) {}
    Spring *springFor(QToolButton *button);
    void aim(QToolButton *button, bool hovered);
    QHash<const QToolButton *, Spring *> m_springs;
    QHash<const QToolButton *, qreal> m_levels;
};

// The checked button of an exclusive group shows one fill that glides to the
// next choice (motion::kFast), instead of one fill going out and another coming on.
// It paints from a layer under the buttons, so their own checked fill stays off.
class SelectionGlide : public QObject {
    Q_OBJECT
public:
    SelectionGlide(QButtonGroup *group, QWidget *surface, theme::Tone tone);
    QRectF fillRect() const;   // where the fill shows now, in surface coordinates; for tests

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void follow(bool animate);
    QButtonGroup *m_group;
    QWidget *m_surface;
    QWidget *m_layer;
    theme::Tone m_tone;
    Spring *m_x, *m_y;
    QPointer<QAbstractButton> m_shown;
};

}
