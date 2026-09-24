#pragma once
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include "studiostyle.h"

namespace eddy {

// One output frame of the frame-rendered video export: the camera's window
// of the source and its annotations, framed like the filter-graph export
// (studio plan 4.2). Safe to use from several threads at once.
class StudioRenderer {
public:
    // `source` is the decoded frame size in document pixels, `content` the crop
    // (or the whole frame), `overlay` the annotations over the whole document at
    // any resolution, or null.
    StudioRenderer(QSize source, QRect content, const StudioStyle &style, const QImage &overlay);

    QSize outputSize() const { return m_output; }
    // `camera` is in document pixels; `out` must be outputSize().
    void render(const QImage &frame, const QRectF &camera, QImage &out) const;

private:
    QSize m_source;
    QSize m_output;
    QRectF m_target;   // where the camera's window lands in the output
    QImage m_frame;    // background with a rounded hole, drawn over the content
    QImage m_overlay;
};

}
