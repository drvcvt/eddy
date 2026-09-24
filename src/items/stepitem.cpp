#include "items/stepitem.h"
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace eddy {

static qreal baseDiameter(StepItem::Size size) {
    switch (size) {
    case StepItem::Size::S: return 28;
    case StepItem::Size::L: return 56;
    default: return 40;
    }
}

StepItem::StepItem(int number, Size size) : m_number(std::clamp(number, 1, kMaxNumber)), m_size(size) {}

void StepItem::setNumber(int number) {
    prepareGeometryChange();
    m_number = std::clamp(number, 1, kMaxNumber);
    update();
}

void StepItem::setSize(Size size) {
    prepareGeometryChange();
    m_size = size;
    update();
}

QFont StepItem::font() const {
    QFont f;
    f.setBold(true);
    f.setPixelSize(qRound(baseDiameter(m_size) * 0.5));
    return f;
}

qreal StepItem::diameter() const {
    const qreal base = baseDiameter(m_size);
    const qreal digits = QFontMetricsF(font()).tightBoundingRect(QString::number(m_number)).width();
    return std::max(base, digits + base * 0.5);
}

StepItem *StepItem::clone() const {
    auto *copy = new StepItem(m_number, m_size);
    copy->setStrokeColor(m_stroke);
    copy->setStrokeWidth(m_width);
    return copy;
}

QRectF StepItem::boundingRect() const {
    const qreal r = diameter() / 2 + 1;
    return QRectF(-r, -r, 2 * r, 2 * r);
}

QPainterPath StepItem::shape() const {
    QPainterPath path;
    const qreal r = diameter() / 2;
    path.addEllipse(QPointF(), r, r);
    return path;
}

void StepItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, true);
    const qreal r = diameter() / 2;
    p->setPen(Qt::NoPen);
    p->setBrush(m_stroke);
    p->drawEllipse(QPointF(), r, r);
    // Digits centred by their ink, not by the font's line box.
    const QString text = QString::number(m_number);
    const QFont f = font();
    const QRectF ink = QFontMetricsF(f).tightBoundingRect(text);
    // Relative luminance: yellow and white get dark digits, red and blue white ones.
    const bool light = 0.2126 * m_stroke.redF() + 0.7152 * m_stroke.greenF() + 0.0722 * m_stroke.blueF() > 0.55;
    p->setFont(f);
    p->setPen(light ? QColor(20, 20, 20) : Qt::white);
    p->drawText(QPointF(-ink.center().x(), -ink.center().y()), text);
}

int StepItem::nextNumber(const QGraphicsScene *scene) {
    int highest = 0;
    if (scene)
        for (const QGraphicsItem *item : scene->items())
            if (auto *step = dynamic_cast<const StepItem *>(item)) highest = std::max(highest, step->number());
    return std::min(highest + 1, kMaxNumber);
}

}
