#include "loupe.h"
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <algorithm>

namespace eddy {

QRect loupeCropRect(const QPoint &srcPos, int cells) {
    const int half = cells / 2;
    return QRect(srcPos.x() - half, srcPos.y() - half, cells, cells);
}

QColor loupeSampleColor(const QImage &source, const QPoint &srcPos) {
    if (source.isNull()) return QColor(Qt::black);
    const int x = std::clamp(srcPos.x(), 0, source.width() - 1);
    const int y = std::clamp(srcPos.y(), 0, source.height() - 1);
    return source.pixelColor(x, y);
}

namespace {
constexpr int kSquare = 116;   // magnified area, logical px
constexpr int kLabelH = 22;    // hex readout strip
constexpr int kPad    = 4;     // panel padding around the square
constexpr int kRadius = 11;    // squircle corners
}

Loupe::Loupe(QWidget *parent) : QWidget(parent) {
    setObjectName("Loupe");
    setAttribute(Qt::WA_TransparentForMouseEvents, true);   // never eat the picker's click
    setFixedSize(kSquare + 2 * kPad, kSquare + kLabelH + 2 * kPad);
}

void Loupe::showAt(const QPoint &viewPos, const QImage &source, const QPoint &srcPos) {
    m_source = source;
    m_srcPos = srcPos;
    m_color  = loupeSampleColor(source, srcPos);

    // Offset from the cursor so the loupe never covers the sampled point; flip
    // near the right/bottom edge of the parent so it stays fully visible.
    const int off = 18;
    int x = viewPos.x() + off;
    int y = viewPos.y() + off;
    if (parentWidget()) {
        const QSize ps = parentWidget()->size();
        if (x + width()  > ps.width())  x = viewPos.x() - off - width();
        if (y + height() > ps.height()) y = viewPos.y() - off - height();
        x = std::clamp(x, 0, std::max(0, ps.width()  - width()));
        y = std::clamp(y, 0, std::max(0, ps.height() - height()));
    }
    move(x, y);
    raise();
    show();
    update();
}

void Loupe::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Panel.
    const QRectF panel(0.5, 0.5, width() - 1.0, height() - 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#1a1a1a"));
    p.drawRoundedRect(panel, kRadius, kRadius);

    // Magnified pixel cells (nearest-neighbour grid), clipped to a squircle.
    const QRectF square(kPad, kPad, kSquare, kSquare);
    QPainterPath clip;
    clip.addRoundedRect(square, kRadius - 3, kRadius - 3);
    p.save();
    p.setClipPath(clip);
    p.fillRect(square, QColor("#121212"));
    const int cells = kLoupeCells;
    const int half = cells / 2;
    const qreal cw = square.width()  / cells;
    const qreal ch = square.height() / cells;
    for (int gy = 0; gy < cells; ++gy) {
        for (int gx = 0; gx < cells; ++gx) {
            const int sx = m_srcPos.x() - half + gx;
            const int sy = m_srcPos.y() - half + gy;
            if (sx < 0 || sy < 0 || sx >= m_source.width() || sy >= m_source.height())
                continue;
            p.fillRect(QRectF(square.left() + gx * cw, square.top() + gy * ch,
                              cw + 0.5, ch + 0.5),
                       m_source.pixelColor(sx, sy));
        }
    }
    // Subtle pixel grid.
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    for (int i = 1; i < cells; ++i) {
        p.drawLine(QPointF(square.left() + i * cw, square.top()),
                   QPointF(square.left() + i * cw, square.bottom()));
        p.drawLine(QPointF(square.left(), square.top() + i * ch),
                   QPointF(square.right(), square.top() + i * ch));
    }
    // Centre-pixel marker (white square with a dark outer keyline for contrast).
    const QRectF centre(square.left() + half * cw, square.top() + half * ch, cw, ch);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 170), 1));
    p.drawRect(centre.adjusted(-1, -1, 1, 1));
    p.setPen(QPen(QColor("#ffffff"), 1.4));
    p.drawRect(centre);
    p.restore();

    // Square keyline.
    p.setPen(QPen(QColor(0, 0, 0, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(square, kRadius - 3, kRadius - 3);

    // Hex readout: a small colour chip + the value.
    const QRectF label(kPad, kPad + kSquare, kSquare, kLabelH);
    const QRectF chip(label.left() + 2, label.center().y() - 6, 12, 12);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1));
    p.setBrush(m_color);
    p.drawRoundedRect(chip, 3, 3);
    p.setPen(QColor("#d0d0d0"));
    QFont f = p.font();
    f.setPointSizeF(9.5);
    f.setBold(true);
    p.setFont(f);
    p.drawText(label.adjusted(20, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
               m_color.name(QColor::HexRgb).toUpper());

    // Panel border.
    p.setPen(QPen(QColor("#2a2a2a"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(panel, kRadius, kRadius);
}

}
