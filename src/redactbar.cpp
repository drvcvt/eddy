#include "redactbar.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QVector>

namespace eddy {

RedactBar::RedactBar(QWidget *parent) : QWidget(parent) {
    setObjectName("RedactBar");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);
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
        b->setFixedHeight(26);
        group->addButton(b);
        m_btns.insert(int(m.mode), b);
        connect(b, &QToolButton::clicked, this, [this, mode = m.mode] { emit modeChosen(mode); });
        lay->addWidget(b);
    }
}

void RedactBar::setMode(RedactMode m) {
    if (auto *b = m_btns.value(int(m), nullptr)) b->setChecked(true);
}

}
