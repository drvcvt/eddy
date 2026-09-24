#include "zoombar.h"
#include "motionicon.h"
#include "theme.h"
#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

namespace eddy {

static QMenu *popupMenu(QToolButton *owner, const QString &name) {
    auto *menu = new QMenu(owner);
    menu->setObjectName(name);
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    return menu;
}

ZoomBar::ZoomBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ZoomBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    auto button = [&](const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        layout->addWidget(b);
        return b;
    };
    m_scale = button(QStringLiteral("ZoomScale"), tr("Zoom level\nScroll over the map for any level"));
    m_scale->setPopupMode(QToolButton::InstantPopup);
    auto *scales = popupMenu(m_scale, QStringLiteral("ZoomScaleMenu"));
    for (double s : {1.25, 1.5, 2.0, 3.0}) {
        auto *action = scales->addAction(QString::number(s, 'g', 3) + QStringLiteral("×"));
        action->setCheckable(true);
        action->setData(s);
        connect(action, &QAction::triggered, this, [this, s] { emit scaleChosen(s); });
    }
    m_scale->setMenu(scales);
    m_point = button(QStringLiteral("ZoomTarget"), tr("Zoom on a fixed point"));
    m_point->setText(tr("Point"));
    m_point->setCheckable(true);
    m_cursor = button(QStringLiteral("ZoomTarget"), tr("Follow the pointer"));
    m_cursor->setText(tr("Cursor"));
    m_cursor->setCheckable(true);
    connect(m_point, &QToolButton::clicked, this, [this] { emit targetChosen(ZoomSegment::Target::Point); });
    connect(m_cursor, &QToolButton::clicked, this, [this] { emit targetChosen(ZoomSegment::Target::Cursor); });
    setCursorAvailable(false);
    m_motion = button(QStringLiteral("ZoomMotion"), tr("How the camera moves into and out of this zoom"));
    m_motion->setPopupMode(QToolButton::InstantPopup);
    auto *motions = popupMenu(m_motion, QStringLiteral("ZoomMotionMenu"));
    for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                        ZoomSegment::Motion::Instant}) {
        auto *action = motions->addAction(motionName(motion));
        action->setCheckable(true);
        action->setData(QVariant::fromValue(motion));
        connect(action, &QAction::triggered, this, [this, motion] { emit motionChosen(motion); });
    }
    m_motion->setMenu(motions);
    auto *remove = button(QStringLiteral("ZoomRemove"), tr("Remove this zoom\tDelete"));
    remove->setText(tr("Remove"));
    connect(remove, &QToolButton::clicked, this, &ZoomBar::removeRequested);
    refreshTheme();
}

void ZoomBar::setZoom(const ZoomSegment &zoom) {
    theme::setMenuLabel(m_scale, QString::number(zoom.scale, 'g', 3) + QStringLiteral("×"));
    for (QAction *a : m_scale->menu()->actions()) a->setChecked(qFuzzyCompare(a->data().toDouble(), zoom.scale));
    m_point->setChecked(zoom.target == ZoomSegment::Target::Point);
    m_cursor->setChecked(zoom.target == ZoomSegment::Target::Cursor);
    theme::setMenuLabel(m_motion, motionName(zoom.motion));
    for (QAction *a : m_motion->menu()->actions())
        a->setChecked(a->data().value<ZoomSegment::Motion>() == zoom.motion);
    adjustSize();
}

void ZoomBar::setCursorAvailable(bool available) {
    m_cursor->setEnabled(available);
    m_cursor->setToolTip(available ? tr("Follow the pointer") : tr("Needs a Boltsnap cursor track"));
}

void ZoomBar::refreshTheme() {
    theme::setMenuArrow(m_scale);
    theme::setMenuArrow(m_motion);
    const QColor ink = QApplication::palette().color(QPalette::WindowText);
    for (QAction *a : m_motion->menu()->actions())
        a->setIcon(motionIcon(a->data().value<ZoomSegment::Motion>(), ink, theme::kFsSmall));
}

}
