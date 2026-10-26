#pragma once
// Internal to the EditorWindow translation units, which split the class by topic.
#include <QBoxLayout>
#include <QEvent>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>
#include "app/config.h"
#include "output/exporter.h"

namespace eddy {


// The playback row folds its trim fields onto a second line when the window gets narrow.
class PlaybackBar : public QWidget {
public:
    using QWidget::QWidget;
    QVBoxLayout *stack = nullptr;
    QHBoxLayout *transport = nullptr;
    QWidget *trim = nullptr;
    QWidget *volume = nullptr;
    QSize minimumSizeHint() const override { return {0, QWidget::minimumSizeHint().height()}; }
    void adapt() {
        if (!trim || m_adapting) return;
        m_adapting = true;
        int needed = trim->minimumSizeHint().width() + 24;
        for (int i = 0; i < transport->count(); ++i) {
            auto *item = transport->itemAt(i);
            if (item->widget() == trim) continue;
            needed += (item->widget() == volume ? volume->sizeHint().width() : item->minimumSize().width())
                + transport->spacing();
        }
        const bool wide = width() >= needed;
        if (wide != m_wide) {
            m_wide = wide;
            if (wide) { stack->removeWidget(trim); transport->insertWidget(3, trim); }
            else { transport->removeWidget(trim); stack->addWidget(trim); }
            volume->setVisible(wide);
            updateGeometry();
        }
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
// Above the item if there is room, else below, else wherever it covers less of it.
QPoint contextBarPosition(const QRect &item, const QSize &bar, const QSize &viewport);
DeliverResult copyVideoAtomically(const QString &sourcePath, const QString &destinationPath);

} // namespace eddy
