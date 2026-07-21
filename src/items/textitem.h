#pragma once
#include <QGraphicsTextItem>
#include <QColor>
#include <QFont>
namespace eddy {
enum class TextLabelStyle { Plain, Filled };

struct TextState {
    QString text;
    QFont font;
    QColor color;
    qreal width = -1;
    Qt::Alignment alignment = Qt::AlignLeft;
    TextLabelStyle labelStyle = TextLabelStyle::Plain;
    bool operator==(const TextState &other) const;
};

// A thin wrapper: a styled text annotation. Uses QGraphicsTextItem so the
// canvas can enable inline editing via setTextInteractionFlags().
class TextItem : public QGraphicsTextItem {
public:
    enum { Type = QGraphicsItem::UserType + 2 };
    int type() const override { return Type; }
    explicit TextItem(const QString &text, const QColor &color, qreal pointSize);
    TextItem *clone() const;
    TextState state() const;
    void applyState(const TextState &state);
    QColor annotationColor() const { return m_color; }
    void setAnnotationColor(const QColor &color);
    TextLabelStyle labelStyle() const { return m_labelStyle; }
    void setLabelStyle(TextLabelStyle style);
    Qt::Alignment alignment() const;
    void setAlignment(Qt::Alignment alignment);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
private:
    void updateForeground();
    QColor m_color;
    TextLabelStyle m_labelStyle = TextLabelStyle::Plain;
};
}
