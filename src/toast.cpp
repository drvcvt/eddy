#include "toast.h"
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>
#include <QToolButton>

namespace eddy {

Toast::Toast(QWidget *parent) : QWidget(parent) {
    setObjectName("Toast");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);   // never blocks the canvas
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    m_label = new QLabel(this);
    m_label->setObjectName("ToastText");
    lay->addWidget(m_label);
    m_action = new QToolButton(this);
    m_action->setObjectName("ToastAction");
    m_action->setCursor(Qt::PointingHandCursor);
    m_action->hide();
    lay->addWidget(m_action);
    connect(m_action, &QToolButton::clicked, this, [this] {
        hide();
        if (auto run = std::exchange(m_run, {})) run();
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { hide(); m_run = {}; });
    hide();
}

void Toast::showAction(const QString &text, const QString &action, std::function<void()> run, int ms) {
    m_run = std::move(run);
    m_action->setText(action);
    m_action->show();
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    present(text, ms);
}

// A plain message replaces an offer along with its button.
void Toast::showMessage(const QString &text, int ms) {
    m_run = {};
    m_action->hide();
    setAttribute(Qt::WA_TransparentForMouseEvents, true);   // never blocks the canvas
    present(text, ms);
}

void Toast::present(const QString &text, int ms) {
    m_label->setText(text);
    adjustSize();
    if (QWidget *p = parentWidget()) {
        const int x = (p->width() - width()) / 2;
        const int y = p->height() - height() - 48;   // clears the footer bar
        move(qMax(0, x), qMax(0, y));
    }
    show();
    raise();
    m_timer->start(ms);
}

QString Toast::text() const { return m_label->text(); }

}
