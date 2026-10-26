#include "chrome/toast.h"
#include "app/theme.h"
#include "app/spring.h"
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>
#include <QToolButton>

namespace eddy {

static constexpr int kMaxWidth = 480;

Toast::Toast(QWidget *parent) : QWidget(parent) {
    setObjectName("Toast");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);   // never blocks the canvas
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(theme::kS3, theme::kS3, theme::kS3, theme::kS3);
    m_label = new QLabel(this);
    m_label->setObjectName("ToastText");
    m_label->setWordWrap(true);   // a long error wraps instead of leaving the window
    lay->addWidget(m_label);
    m_action = new QToolButton(this);
    m_action->setObjectName("ToastAction");
    m_action->setCursor(Qt::PointingHandCursor);
    m_action->setFixedHeight(theme::kCtl);
    m_action->hide();
    lay->addWidget(m_action);
    connect(m_action, &QToolButton::clicked, this, [this] {
        auto run = std::exchange(m_run, {});
        dismiss();
        if (run) run();
    });

    m_opacity = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacity);
    m_fade = new Spring(motion::kBase, [this](qreal opacity) {
        m_opacity->setOpacity(opacity);
        if (opacity <= 0 && m_fade->target() <= 0) hide();
    }, this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { m_run = {}; dismiss(); });
    hide();
}

void Toast::dismiss() {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_timer->stop();
    m_fade->setTarget(0);
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
    if (QWidget *p = parentWidget()) setMaximumWidth(qMin(kMaxWidth, p->width() - 2 * theme::kS4));
    adjustSize();
    if (QWidget *p = parentWidget()) {
        const int x = (p->width() - width()) / 2;
        const int y = p->height() - height() - theme::kBar - theme::kS2;   // clears the footer bar
        move(qMax(0, x), qMax(0, y));
    }
    if (isHidden()) m_fade->jumpTo(0);
    show();
    raise();
    m_fade->setTarget(1);
    m_timer->start(ms);
}

QString Toast::text() const { return m_label->text(); }

}
