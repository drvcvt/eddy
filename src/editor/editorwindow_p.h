#pragma once
// Internal to the EditorWindow translation units, which split the class by topic.
#include <QBoxLayout>
#include <QGridLayout>
#include <QEvent>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>
#include <QPainter>
#include "app/theme.h"
#include "app/config.h"
#include "output/exporter.h"
#include "app/spring.h"
#include "video/mediaio.h"

namespace eddy {


// The playback row folds its trim fields onto a second line when the window gets narrow.
// Start, End and Duration on one recessed field: the workspace's deeper grey on
// squircle corners, no outline. Its controls keep their own hover fills on top.
class TrimField : public QWidget {
public:
    using QWidget::QWidget;
    static constexpr int kInset = 2;   // around the kCtl controls: the field is kCtl + 4 high
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::tone(theme::Tone::Well, palette().color(QPalette::Window).lightness() < 128));
        painter.drawPath(theme::squircleRect(QRectF(rect()), theme::kRChip + kInset + 2));
    }
};

// Three columns: play and time left, the trim group centred on the window,
// loop, rate and sound right. The outer columns share one width, so the
// middle sits on the window's centre (and on the drag pill's below).
class PlaybackBar : public QWidget {
public:
    using QWidget::QWidget;
    QVBoxLayout *stack = nullptr;
    QGridLayout *transport = nullptr;
    QHBoxLayout *left = nullptr, *right = nullptr;
    QWidget *trim = nullptr;
    QSize minimumSizeHint() const override { return {0, QWidget::minimumSizeHint().height()}; }
    void adapt() {
        if (!trim || m_adapting) return;
        m_adapting = true;
        const int side = qMax(left->minimumSize().width(), right->minimumSize().width());
        const int needed = trim->minimumSizeHint().width() + 2 * side + 2 * transport->horizontalSpacing()
                           + contentsMargins().left() + contentsMargins().right();
        const bool wide = width() >= needed;
        if (wide != m_wide) {
            m_wide = wide;
            if (wide) { stack->removeWidget(trim); transport->addWidget(trim, 0, 1, Qt::AlignCenter); }
            else { transport->removeWidget(trim); stack->addWidget(trim); }
            updateGeometry();
        }
        const int balanced = wide ? side : 0;
        transport->setColumnMinimumWidth(0, balanced);
        transport->setColumnMinimumWidth(2, balanced);
        m_adapting = false;
    }
protected:
    bool event(QEvent *event) override {
        const bool result = QWidget::event(event);
        if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest) adapt();
        return result;
    }
private:
    bool m_wide = true, m_adapting = false;
};

#ifdef Q_OS_WIN
void applyWindowsTitleBarTheme(QWidget *window, bool dark);
#endif
// The playback readout: position and length to the millisecond, like the trim fields.
inline QString playbackTime(qint64 position, qint64 duration) {
    return formatPreciseTime(position) + QStringLiteral(" / ") + formatPreciseTime(duration);
}

// Above the item if there is room, else below, else wherever it covers less of it.
QPoint contextBarPosition(const QRect &item, const QSize &bar, const QSize &viewport);
DeliverResult copyVideoAtomically(const QString &sourcePath, const QString &destinationPath);

} // namespace eddy
