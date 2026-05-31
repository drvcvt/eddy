#pragma once
#include <QGraphicsTextItem>
#include <QColor>
namespace eddy {
// A thin wrapper: a styled text annotation. Uses QGraphicsTextItem so the
// canvas can enable inline editing via setTextInteractionFlags().
class TextItem : public QGraphicsTextItem {
public:
    enum { Type = QGraphicsItem::UserType + 2 };
    int type() const override { return Type; }
    explicit TextItem(const QString &text, const QColor &color, qreal pointSize);
};
}
