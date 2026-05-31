#pragma once
#include <QWidget>
#include <QColor>
namespace eddy {
// A small popup of preset colour swatches plus a "Custom…" fallback.
class ColorPopover : public QWidget {
    Q_OBJECT
public:
    explicit ColorPopover(QWidget *parent = nullptr);
signals:
    void picked(const QColor &c);
};
}
