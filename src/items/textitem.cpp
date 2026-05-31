#include "items/textitem.h"
#include <QFont>
namespace eddy {
TextItem::TextItem(const QString &text, const QColor &color, qreal pointSize) {
    setPlainText(text);
    setDefaultTextColor(color);
    QFont f = font(); f.setPointSizeF(pointSize); setFont(f);
}
}
