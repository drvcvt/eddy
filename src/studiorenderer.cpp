#include "studiorenderer.h"
#include <QPainter>
#include <algorithm>

namespace eddy {

StudioRenderer::StudioRenderer(QSize source, QRect content, const StudioStyle &style, const QImage &overlay)
    : m_source(source), m_overlay(overlay) {
    const StudioLayout layout = studioLayout(content.size(), style);
    m_output = layout.output;
    m_target = QRectF(layout.content);
    if (style.active()) {
        // The same background and coverage mask the filter graph merges, as one
        // image with the mask for alpha: opaque around the content, clear over it.
        m_frame = renderStudioBackground(content.size(), style)
                      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_frame.setAlphaChannel(renderStudioFrameMask(content.size(), style));
    }
}

void StudioRenderer::render(const QImage &frame, const QRectF &camera, QImage &out, double sourceMs) const {
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(m_target, frame, camera);
    auto overlay = [&](const QImage &image) {
        const qreal sx = qreal(image.width()) / m_source.width();
        const qreal sy = qreal(image.height()) / m_source.height();
        p.drawImage(m_target, image,
                    QRectF(camera.x() * sx, camera.y() * sy, camera.width() * sx, camera.height() * sy));
    };
    if (!m_overlay.isNull()) overlay(m_overlay);
    for (const TimedOverlay &timed : m_timed)
        if (sourceMs >= timed.fromMs && sourceMs < timed.toMs) overlay(timed.image);
    if (!m_frame.isNull()) p.drawImage(0, 0, m_frame);
}

void StudioRenderer::renderBlurred(const QImage &frame, const QVector<QRectF> &cameras, QImage &out,
                                   double sourceMs) const {
    if (cameras.isEmpty()) return;
    render(frame, cameras.first(), out, sourceMs);
    if (std::all_of(cameras.cbegin(), cameras.cend(), [&](const QRectF &c) { return c == cameras.first(); })) return;
    // A running mean: the n-th image goes in at 1/n.
    QImage sub(out.size(), out.format());
    QPainter p(&out);
    for (int i = 1; i < cameras.size(); ++i) {
        render(frame, cameras[i], sub, sourceMs);
        p.setOpacity(1.0 / (i + 1));
        p.drawImage(0, 0, sub);
    }
}

}
