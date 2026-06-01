#include "toast.h"
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>

namespace eddy {

Toast::Toast(QWidget *parent) : QWidget(parent) {
    setObjectName("Toast");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);   // never blocks the canvas
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 7, 12, 7);
    m_label = new QLabel(this);
    m_label->setObjectName("ToastText");
    lay->addWidget(m_label);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { hide(); });
    hide();
}

void Toast::showMessage(const QString &text, int ms) {
    m_label->setText(text);
    adjustSize();
    if (QWidget *p = parentWidget()) {
        const int x = (p->width() - width()) / 2;
        const int y = p->height() - height() - 24;
        move(qMax(0, x), qMax(0, y));
    }
    show();
    raise();
    m_timer->start(ms);
}

QString Toast::text() const { return m_label->text(); }

}
