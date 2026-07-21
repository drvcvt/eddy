#include "items/textitem.h"
#include <QFont>
#include <QTextDocument>
#include <QTextOption>
#include <QPainter>
namespace eddy {
bool TextState::operator==(const TextState &other) const {
    return text == other.text && font == other.font && color == other.color
        && width == other.width && alignment == other.alignment
        && labelStyle == other.labelStyle;
}

TextItem::TextItem(const QString &text, const QColor &color, qreal pointSize)
    : m_color(color) {
    setPlainText(text);
    QFont f = font(); f.setPointSizeF(pointSize); setFont(f);
    updateForeground();
}

TextItem *TextItem::clone() const {
    auto *copy = new TextItem(toPlainText(), m_color, font().pointSizeF());
    copy->applyState(state());
    copy->setTextInteractionFlags(textInteractionFlags());
    return copy;
}

TextState TextItem::state() const {
    return {toPlainText(), font(), m_color, textWidth(), alignment(), m_labelStyle};
}

void TextItem::applyState(const TextState &value) {
    setPlainText(value.text);
    setFont(value.font);
    setTextWidth(value.width);
    m_color = value.color;
    m_labelStyle = value.labelStyle;
    setAlignment(value.alignment);
    updateForeground();
    update();
}

void TextItem::setAnnotationColor(const QColor &color) {
    m_color = color;
    updateForeground();
    update();
}

void TextItem::setLabelStyle(TextLabelStyle style) {
    if (m_labelStyle == style) return;
    prepareGeometryChange();
    m_labelStyle = style;
    updateForeground();
    update();
}

Qt::Alignment TextItem::alignment() const {
    return document()->defaultTextOption().alignment();
}

void TextItem::setAlignment(Qt::Alignment value) {
    QTextOption option = document()->defaultTextOption();
    option.setAlignment(value);
    document()->setDefaultTextOption(option);
    update();
}

void TextItem::updateForeground() {
    if (m_labelStyle == TextLabelStyle::Plain) {
        setDefaultTextColor(m_color);
        return;
    }
    setDefaultTextColor(m_color.lightness() > 145 ? QColor("#1A1A1A") : QColor("#FAFAFA"));
}

QRectF TextItem::boundingRect() const {
    const QRectF base = QGraphicsTextItem::boundingRect();
    return m_labelStyle == TextLabelStyle::Filled ? base.adjusted(-6, -3, 6, 3) : base;
}

void TextItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    if (m_labelStyle == TextLabelStyle::Filled) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_color);
        painter->drawRoundedRect(boundingRect(), 9, 9);
    }
    QGraphicsTextItem::paint(painter, option, widget);
}
}
