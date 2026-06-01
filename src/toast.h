#pragma once
#include <QWidget>

class QLabel;
class QTimer;

namespace eddy {

// A small transient message overlay, anchored at the bottom-centre of its parent.
class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget *parent = nullptr);
    void showMessage(const QString &text, int ms = 2500);
    QString text() const;

private:
    QLabel *m_label;
    QTimer *m_timer;
};

}
