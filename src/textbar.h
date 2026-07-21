#pragma once
#include <QWidget>
#include "items/textitem.h"
class QToolButton;

namespace eddy {
class TextBar : public QWidget {
    Q_OBJECT
public:
    explicit TextBar(QWidget *parent = nullptr);
    void setState(const TextState &state);
    void refreshTheme();
signals:
    void sizeChosen(qreal size);
    void boldChosen(bool bold);
    void alignmentChosen(Qt::Alignment alignment);
    void styleChosen(TextLabelStyle style);
private:
    QToolButton *m_sizes[3]{};
    QToolButton *m_bold = nullptr;
    QToolButton *m_align[3]{};
    QToolButton *m_filled = nullptr;
};
}
