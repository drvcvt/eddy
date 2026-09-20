#pragma once
#include <QWidget>
#include <QColor>
namespace eddy {
// A small popup of preset colour swatches plus a "Custom…" fallback.
class ColorPopover : public QWidget {
    Q_OBJECT
public:
    explicit ColorPopover(QWidget *parent = nullptr, const QColor &current = QColor("#ff3b30"));
signals:
    void picked(const QColor &c);
    void customRequested(const QColor &current);
    void eyedropperRequested();   // user chose the pipette: sample a colour off the canvas
};
}
