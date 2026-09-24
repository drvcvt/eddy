#include "studiorenderer.h"
#include <QPainter>

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

void StudioRenderer::render(const QImage &frame, const QRectF &camera, QImage &out) const {
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(m_target, frame, camera);
    if (!m_overlay.isNull()) {
        const qreal sx = qreal(m_overlay.width()) / m_source.width();
        const qreal sy = qreal(m_overlay.height()) / m_source.height();
        p.drawImage(m_target, m_overlay,
                    QRectF(camera.x() * sx, camera.y() * sy, camera.width() * sx, camera.height() * sy));
    }
    if (!m_frame.isNull()) p.drawImage(0, 0, m_frame);
}

}
