#include "items/labels/stepitem.h"
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace eddy {

static qreal baseDiameter(StepItem::Size size) {
    switch (size) {
    case StepItem::Size::S: return 28;
    case StepItem::Size::L: return 56;
    default: return 40;
    }
}

static quint64 s_serial = 0;

// Qt flattens a small ellipse into facets you can see, its edge coverage off
// by up to a sixth. A polygon whose sides stay within 1/100 px of the circle
// at the device size antialiases almost exactly, zoomed in or not.
static QPolygonF disc(qreal r, const QTransform &device) {
    const qreal onDevice = std::max<qreal>(1, r * std::sqrt(std::abs(device.determinant())));
    const int n = std::clamp(int(std::ceil(M_PI / std::sqrt(0.02 / onDevice))), 32, 2048);
    QPolygonF polygon(n);
    for (int i = 0; i < n; ++i) polygon[i] = QPointF(r * std::cos(2 * M_PI * i / n), r * std::sin(2 * M_PI * i / n));
    return polygon;
}

StepItem::StepItem(int number, Size size)
    : m_number(std::clamp(number, 1, kMaxNumber)), m_size(size), m_serial(++s_serial) {
    measure();
}

void StepItem::setNumber(int number) {
    prepareGeometryChange();
    m_number = std::clamp(number, 1, kMaxNumber);
    measure();
    update();
}

void StepItem::setSize(Size size) {
    prepareGeometryChange();
    m_size = size;
    measure();
    update();
}

QFont StepItem::font() const {
    QFont f;
    f.setBold(true);
    f.setPixelSize(qRound(baseDiameter(m_size) * 0.5));
    return f;
}

void StepItem::measure() {
    const qreal base = baseDiameter(m_size);
    const qreal digits = QFontMetricsF(font()).tightBoundingRect(QString::number(m_number)).width();
    m_diameter = std::max(base, digits + base * 0.5);
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
    p->drawPolygon(disc(r, p->deviceTransform()));
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
