#include "studiostyle.h"
#include <QFileInfo>
#include <QImageReader>
#include <QDateTime>
#include <QLinearGradient>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace eddy {

static double unit(QSize content) {
    return std::min(content.width(), content.height()) / 100.0;
}

QList<StudioBackgroundPreset> studioBackgroundPresets() {
    using B = StudioStyle::Background;
    return {
        {QStringLiteral("Graphite"), B::Gradient, QColor("#3a3a3a"), QColor("#0e0e0e")},
        {QStringLiteral("Paper"), B::Gradient, QColor("#f4f4f4"), QColor("#cfcfcf")},
        {QStringLiteral("Dusk"), B::Gradient, QColor("#5b4bdb"), QColor("#ff7eb3")},
        {QStringLiteral("Ocean"), B::Gradient, QColor("#0b3d6b"), QColor("#4fc3dc")},
        {QStringLiteral("Sunset"), B::Gradient, QColor("#ff9a3d"), QColor("#ff3b6b")},
        {QStringLiteral("Mint"), B::Gradient, QColor("#137c6f"), QColor("#9fe3a0")},
    };
}

StudioLayout studioLayout(QSize content, const StudioStyle &style) {
    if (!style.active() || content.isEmpty()) return {content, QRect(QPoint(), content)};
    const int pad = qRound(std::clamp(style.padding, 0.0, 50.0) * unit(content));
    QSize output = content + QSize(2 * pad, 2 * pad);
    if (!style.aspect.isEmpty()) {
        // Grow one side until the output has the requested ratio; never crop.
        const double want = double(style.aspect.width()) / style.aspect.height();
        if (double(output.width()) / output.height() < want)
            output.setWidth(qCeil(output.height() * want));
        else
            output.setHeight(qCeil(output.width() / want));
    }
    // Even sizes and an even origin keep the layout exact for yuv420p video:
    // ffmpeg's pad would silently round an odd offset down, and the video
    // would miss the frame's hole by a pixel.
    output = QSize(output.width() + output.width() % 2, output.height() + output.height() % 2);
    const QPoint origin(((output.width() - content.width()) / 2) & ~1,
                        ((output.height() - content.height()) / 2) & ~1);
    return {output, QRect(origin, content), std::clamp(style.radius, 0.0, 50.0) * unit(content)};
}

QRect keepZoomedInRect(QRect content, const StudioStyle &style, QPointF center) {
    if (!style.active() || style.aspect.isEmpty() || content.width() < 4 || content.height() < 4)
        return content;
    const double want = double(style.aspect.width()) / style.aspect.height();
    // The ratio of the padded window, before studioLayout grows it to `want`;
    // it rises with the width and falls with the height.
    auto framed = [&](int w, int h) {
        const int pad = qRound(std::clamp(style.padding, 0.0, 50.0) * unit(QSize(w, h)));
        return double(w + 2 * pad) / (h + 2 * pad);
    };
    int w = content.width() & ~1, h = content.height() & ~1;
    if (framed(w, h) > want) {
        int lo = 2, hi = w;   // the widest even window that is not too wide
        while (lo < hi) {
            const int mid = ((lo + hi + 2) / 2) & ~1;
            if (framed(mid, h) <= want) lo = mid; else hi = mid - 2;
        }
        w = lo;
    } else {
        int lo = 2, hi = h;   // the tallest even window that is not too tall
        while (lo < hi) {
            const int mid = ((lo + hi + 2) / 2) & ~1;
            if (framed(w, mid) >= want) lo = mid; else hi = mid - 2;
        }
        h = lo;
    }
    if (w == (content.width() & ~1) && h == (content.height() & ~1)) return content;
    const int x = qBound(0, qRound(center.x() - w / 2.0) - content.x(), content.width() - w) & ~1;
    const int y = qBound(0, qRound(center.y() - h / 2.0) - content.y(), content.height() - h) & ~1;
    return QRect(content.x() + x, content.y() + y, w, h);
}

static QPainterPath roundedContent(const QRectF &rect, double radius) {
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

// Three box-blur passes approximate a Gaussian. Runs on a downscaled alpha
// image; the shadow is soft enough that the upscale does not show.
static void boxBlurAlpha(QImage &image, int radius) {
    if (radius < 1) return;
    const int w = image.width(), h = image.height();
    QVector<int> line(std::max(w, h));
    for (int pass = 0; pass < 3; ++pass) {
        for (int horizontal = 1; horizontal >= 0; --horizontal) {
            const int outer = horizontal ? h : w, inner = horizontal ? w : h;
            for (int o = 0; o < outer; ++o) {
                auto px = [&](int i) -> uchar & {
                    return horizontal ? image.scanLine(o)[i] : image.scanLine(i)[o];
                };
                for (int i = 0; i < inner; ++i) line[i] = px(i);
                int sum = 0;
                for (int i = -radius; i <= radius; ++i) sum += line[std::clamp(i, 0, inner - 1)];
                for (int i = 0; i < inner; ++i) {
                    px(i) = uchar(sum / (2 * radius + 1));
                    sum += line[std::min(i + radius + 1, inner - 1)] - line[std::max(i - radius, 0)];
                }
            }
        }
    }
}

// Slider drags re-render the background many times per second; decoding a
// large wallpaper each time would stall them. One entry is enough, keyed by
// path and modification time. Export threads read it too.
static QImage backgroundImage(const QString &path) {
    static QMutex mutex;
    static QString cachedKey;
    static QImage cached;
    const QFileInfo info(path);
    const QString key = path + QLatin1Char('@') + QString::number(info.lastModified().toMSecsSinceEpoch());
    QMutexLocker lock(&mutex);
    if (key != cachedKey) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        cached = reader.read();
        cachedKey = key;
    }
    return cached;
}

static void paintBackground(QPainter &p, QSize output, const StudioStyle &style) {
    const QRectF rect(QPointF(), output);
    switch (style.background) {
    case StudioStyle::Background::None:
        return;
    case StudioStyle::Background::Color:
        p.fillRect(rect, style.color);
        return;
    case StudioStyle::Background::Gradient: {
        const double a = qDegreesToRadians(double(style.gradientAngle));
        const QPointF dir(std::cos(a), std::sin(a));
        // Project the corners so the gradient spans the whole rect at any angle.
        const double half = (std::abs(dir.x()) * rect.width() + std::abs(dir.y()) * rect.height()) / 2;
        QLinearGradient g(rect.center() - dir * half, rect.center() + dir * half);
        g.setColorAt(0, style.color);
        g.setColorAt(1, style.color2);
        p.fillRect(rect, g);
        return;
    }
    case StudioStyle::Background::Image: {
        p.fillRect(rect, style.color);
        const QImage image = backgroundImage(style.imagePath);
        if (image.isNull()) return;
        // Cover: fill the output, crop the overflow, keep the centre.
        const QSize scaled = image.size().scaled(output, Qt::KeepAspectRatioByExpanding);
        const QRect target(QPoint((output.width() - scaled.width()) / 2,
                                  (output.height() - scaled.height()) / 2), scaled);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, image);
        return;
    }
    }
}

QImage renderStudioBackground(QSize content, const StudioStyle &style) {
    const StudioLayout layout = studioLayout(content, style);
    QImage out(layout.output, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (!style.active()) return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    paintBackground(p, layout.output, style);

    const double u = unit(content);
    const double radius = std::clamp(style.radius, 0.0, 50.0) * u;
    const double strength = std::clamp(style.shadow, 0.0, 100.0) / 100.0;
    if (strength > 0) {
        // Soft, slightly lowered shadow; blur and offset scale with the content.
        constexpr int scale = 4;
        const int blur = std::max(1, qRound(3.0 * u / scale));
        const QSize small((layout.output.width() + scale - 1) / scale,
                          (layout.output.height() + scale - 1) / scale);
        QImage alpha(small, QImage::Format_Alpha8);
        alpha.fill(0);
        {
            QPainter a(&alpha);
            a.setRenderHint(QPainter::Antialiasing);
            a.scale(1.0 / scale, 1.0 / scale);
            a.fillPath(roundedContent(QRectF(layout.content).translated(0, 1.2 * u), radius),
                       QColor(0, 0, 0, qRound(255 * std::min(1.0, 0.35 + 0.5 * strength))));
        }
        boxBlurAlpha(alpha, blur);
        QImage shadow(small, QImage::Format_ARGB32_Premultiplied);
        shadow.fill(Qt::black);
        {
            QPainter s(&shadow);
            s.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            s.drawImage(0, 0, alpha);
        }
        p.setOpacity(strength);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRect(QPoint(), small * scale), shadow);
        p.setOpacity(1);
    }
    return out;
}

QImage renderStudioMask(QSize content, const StudioStyle &style) {
    QImage mask(content, QImage::Format_Grayscale8);
    mask.fill(Qt::black);
    QPainter p(&mask);
    p.setRenderHint(QPainter::Antialiasing);
    const double radius = std::clamp(style.radius, 0.0, 50.0) * unit(content);
    p.fillPath(roundedContent(QRectF(QPointF(), content), radius), Qt::white);
    return mask;
}

StudioStyle loadLastStudioStyle(const QString &configPath) {
    StudioStyle style;
    const auto dusk = studioBackgroundPresets().at(2);
    style.background = dusk.kind;
    style.color = dusk.color;
    style.color2 = dusk.color2;
    if (configPath.isEmpty()) return style;
    QSettings s(configPath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("studio"));
    const QString kind = s.value(QStringLiteral("background")).toString();
    if (kind == QStringLiteral("color")) style.background = StudioStyle::Background::Color;
    else if (kind == QStringLiteral("gradient")) style.background = StudioStyle::Background::Gradient;
    else if (kind == QStringLiteral("image")) style.background = StudioStyle::Background::Image;
    const QColor color(s.value(QStringLiteral("color")).toString());
    const QColor color2(s.value(QStringLiteral("color2")).toString());
    if (color.isValid()) style.color = color;
    if (color2.isValid()) style.color2 = color2;
    style.gradientAngle = s.value(QStringLiteral("angle"), style.gradientAngle).toInt();
    style.imagePath = s.value(QStringLiteral("image")).toString();
    style.padding = std::clamp(s.value(QStringLiteral("padding"), style.padding).toDouble(), 0.0, 50.0);
    style.radius = std::clamp(s.value(QStringLiteral("radius"), style.radius).toDouble(), 0.0, 50.0);
    style.shadow = std::clamp(s.value(QStringLiteral("shadow"), style.shadow).toDouble(), 0.0, 100.0);
    const QStringList aspect = s.value(QStringLiteral("aspect")).toString().split(QLatin1Char(':'));
    if (aspect.size() == 2 && aspect[0].toInt() > 0 && aspect[1].toInt() > 0)
        style.aspect = QSize(aspect[0].toInt(), aspect[1].toInt());
    // An image that has gone missing must not leave Studio without a background.
    if (style.background == StudioStyle::Background::Image && !QFileInfo::exists(style.imagePath))
        style.background = StudioStyle::Background::Gradient;
    return style;
}

void saveLastStudioStyle(const QString &configPath, const StudioStyle &style) {
    if (configPath.isEmpty() || !style.active()) return;
    QSettings s(configPath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("studio"));
    using B = StudioStyle::Background;
    s.setValue(QStringLiteral("background"), style.background == B::Color ? QStringLiteral("color")
        : style.background == B::Image ? QStringLiteral("image") : QStringLiteral("gradient"));
    s.setValue(QStringLiteral("color"), style.color.name());
    s.setValue(QStringLiteral("color2"), style.color2.name());
    s.setValue(QStringLiteral("angle"), style.gradientAngle);
    s.setValue(QStringLiteral("image"), style.imagePath);
    s.setValue(QStringLiteral("padding"), style.padding);
    s.setValue(QStringLiteral("radius"), style.radius);
    s.setValue(QStringLiteral("shadow"), style.shadow);
    s.setValue(QStringLiteral("aspect"), style.aspect.isEmpty() ? QString()
        : QStringLiteral("%1:%2").arg(style.aspect.width()).arg(style.aspect.height()));
}

QImage renderStudioFrameMask(QSize content, const StudioStyle &style) {
    const StudioLayout layout = studioLayout(content, style);
    QImage mask(layout.output, QImage::Format_Grayscale8);
    mask.fill(Qt::white);
    QPainter p(&mask);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(roundedContent(QRectF(layout.content), layout.radius), Qt::black);
    return mask;
}

QImage renderStudioImage(const QImage &content, const StudioStyle &style) {
    if (!style.active() || content.isNull()) return content;
    const StudioLayout layout = studioLayout(content.size(), style);
    QImage out = renderStudioBackground(content.size(), style);
    QImage rounded = content.convertToFormat(QImage::Format_ARGB32);
    rounded.setAlphaChannel(renderStudioMask(content.size(), style));
    QPainter p(&out);
    p.drawImage(layout.content.topLeft(), rounded);
    return out;
}

}
