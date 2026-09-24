#pragma once
#include <QWidget>
#include <optional>
#include "studiodocument.h"
#include "studiostyle.h"

class QLabel;
class QSlider;
class QToolButton;

namespace eddy {

// What the Camera page shows; only videos have one.
struct StudioCameraSettings {
    bool available = false;
    std::optional<ZoomSegment::Motion> motion;   // empty while the zooms differ
    bool keepZoomedIn = false;
    bool keepZoomedInAvailable = false;          // the ratio leaves room to fill
    bool suggestAvailable = false;               // a cursor track came with the video
};

// Studio framing controls: background, spacing, corners, shadow and output
// ratio. Every change is emitted immediately so the canvas previews it.
class StudioPopover : public QWidget {
    Q_OBJECT
public:
    // `content` is the (cropped) media size the output size is derived from.
    StudioPopover(const StudioStyle &style, QSize content, const StudioCameraSettings &camera,
                  QWidget *parent = nullptr);
    StudioStyle studioStyle() const { return m_style; }
    void setKeepZoomedInAvailable(bool available);
    // The framed media's size, which "keep zoomed in" narrows.
    void setContentSize(QSize content);
protected:
    void paintEvent(QPaintEvent *) override;
signals:
    void styleChanged(const StudioStyle &style);
    void imageRequested();   // "Image…": the caller opens a file dialog
    void motionChosen(ZoomSegment::Motion motion);
    void keepZoomedInChanged(bool on);
    void suggestRequested();
private:
    void apply();
    void showSize();
    StudioStyle m_style;
    QList<QToolButton *> m_backgrounds;
    QList<QToolButton *> m_ratios;
    QSize m_content;
    QLabel *m_size = nullptr;
    QSlider *m_padding = nullptr;
    QSlider *m_radius = nullptr;
    QSlider *m_shadow = nullptr;
    QToolButton *m_keep = nullptr;
};

}
