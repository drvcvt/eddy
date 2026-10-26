#pragma once
#include <QWidget>
#include <functional>

class QLabel;
class QToolButton;
class QTimer;
class QGraphicsOpacityEffect;

namespace eddy {

class Spring;

// A small transient message overlay, anchored at the bottom-centre of its parent.
class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget *parent = nullptr);
    void showMessage(const QString &text, int ms = 2500);
    // A message with one action button; the toast takes clicks while it shows.
    void showAction(const QString &text, const QString &action, std::function<void()> run, int ms = 6000);
    QString text() const;

private:
    void present(const QString &text, int ms);
    void dismiss();   // fades out; takes no clicks while it goes
    QLabel *m_label;
    QToolButton *m_action;
    std::function<void()> m_run;
    QTimer *m_timer;
    QGraphicsOpacityEffect *m_opacity;
    Spring *m_fade;
};

}
