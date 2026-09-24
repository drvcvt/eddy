#pragma once
#include <QWidget>
#include "studiostyle.h"

class QLabel;
class QSlider;
class QToolButton;

namespace eddy {

// Studio framing controls: background, spacing, corners, shadow and output
// ratio. Every change is emitted immediately so the canvas previews it.
class StudioPopover : public QWidget {
    Q_OBJECT
public:
    // `content` is the (cropped) media size the output size is derived from.
    StudioPopover(const StudioStyle &style, QSize content, QWidget *parent = nullptr);
    StudioStyle studioStyle() const { return m_style; }
protected:
    void paintEvent(QPaintEvent *) override;
signals:
    void styleChanged(const StudioStyle &style);
    void imageRequested();   // "Image…": the caller opens a file dialog
private:
    void apply();
    StudioStyle m_style;
    QList<QToolButton *> m_backgrounds;
    QList<QToolButton *> m_ratios;
    QSize m_content;
    QLabel *m_size = nullptr;
    QSlider *m_padding = nullptr;
    QSlider *m_radius = nullptr;
    QSlider *m_shadow = nullptr;
};

}
