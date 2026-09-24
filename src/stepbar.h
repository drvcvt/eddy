#pragma once
#include <QWidget>
#include "items/stepitem.h"
class QLineEdit;
class QToolButton;

namespace eddy {
// Context bar of a selected step: its number, S/M/L and a menu to renumber
// every step (21.09. plan 6).
class StepBar : public QWidget {
    Q_OBJECT
public:
    explicit StepBar(QWidget *parent = nullptr);
    void setStep(int number, StepItem::Size size);
    void refreshTheme();
signals:
    void numberChosen(int number);
    void sizeChosen(StepItem::Size size);
    void renumberRequested();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QLineEdit *m_number = nullptr;
    QToolButton *m_sizes[3]{};
    QToolButton *m_more = nullptr;
    int m_current = 1;
};
}
