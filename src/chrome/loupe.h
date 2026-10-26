#pragma once
#include <QWidget>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QRect>

namespace eddy {

// Number of source pixels shown across the loupe (odd -> a centre cell).
inline constexpr int kLoupeCells = 15;

// The device-pixel crop, centred on `srcPos`, that the loupe magnifies.
QRect loupeCropRect(const QPoint &srcPos, int cells = kLoupeCells);

// Colour of `source` at `srcPos`, clamped to the image bounds so edge sampling
// never reads out of range. Returns black for a null image.
QColor loupeSampleColor(const QImage &source, const QPoint &srcPos);

// A boltsnap-style eyedropper loupe: a rounded square that magnifies a frozen
// snapshot around the cursor (pixel grid, centre-pixel marker, hex readout).
// It lives as a child of the canvas viewport, so it needs no global window
// positioning (Wayland-safe). Reposition + refresh it via showAt().
class Loupe : public QWidget {
    Q_OBJECT
public:
    explicit Loupe(QWidget *parent = nullptr);
    // viewPos: cursor in parent (viewport) coords. source: frozen snapshot in
    // device pixels. srcPos: the sampled pixel, also in device pixels.
    void showAt(const QPoint &viewPos, const QImage &source, const QPoint &srcPos);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage m_source;
    QPoint m_srcPos;
    QColor m_color;
};

}
