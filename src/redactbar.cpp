#include "redactbar.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QVector>

namespace eddy {

RedactBar::RedactBar(QWidget *parent) : QWidget(parent) {
    setObjectName("RedactBar");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(2);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);

    struct M { RedactMode mode; const char *label; };
    const QVector<M> modes = {
        {RedactMode::Blur, "Blur"}, {RedactMode::Blacken, "Black"},
        {RedactMode::OcrBlur, "OCR Blur"}, {RedactMode::OcrBlacken, "OCR Black"},
    };
    for (const M &m : modes) {
        auto *b = new QToolButton(this);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);     // keep window hotkeys working
        b->setCursor(Qt::PointingHandCursor);
        b->setText(QString::fromUtf8(m.label));
        b->setFixedHeight(theme::kFloatButton.height());
        group->addButton(b);
        m_btns.insert(int(m.mode), b);
        connect(b, &QToolButton::clicked, this, [this, mode = m.mode] { emit modeChosen(mode); });
        lay->addWidget(b);
    }
    // Videos: the whole clip or from the playhead on (studio plan 6.7, E5).
    auto *scopes = new QButtonGroup(this);
    for (int i = 0; i < 2; ++i) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("TimeScope"));
        b->setText(i ? tr("From playhead") : tr("Whole clip"));
        b->setToolTip(i ? tr("Show it from the playhead to the end, then shorten it on the mask lane")
                        : tr("Show it for the whole clip"));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        b->hide();
        scopes->addButton(b, i);
        lay->addWidget(b);
        m_scopes[i] = b;
    }
    connect(scopes, &QButtonGroup::idClicked, this, [this](int id) { emit timeScopeChosen(id == 1); });
}

void RedactBar::setTimeScope(bool video, bool timed) {
    for (int i = 0; i < 2; ++i) {
        m_scopes[i]->setVisible(video);
        m_scopes[i]->setChecked(timed == (i == 1));
    }
    adjustSize();
}

void RedactBar::setMode(RedactMode m) {
    if (auto *b = m_btns.value(int(m), nullptr)) b->setChecked(true);
}

}
