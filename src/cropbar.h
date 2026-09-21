#pragma once
#include <QWidget>
class QToolButton;
class QLabel;
namespace eddy {
class CropBar : public QWidget {
    Q_OBJECT
public:
    explicit CropBar(QWidget *parent = nullptr);
    void setSourceSize(QSize size) { m_sourceSize = size; }
    void setOutputSize(QSize size);
    void resetRatio();
    void setAvailableWidth(int width);
signals:
    void ratioChosen(qreal ratio);
    void resetRequested();
    void cancelRequested();
    void applyRequested();
private:
    QToolButton *m_ratio;
    QLabel *m_size;
    QSize m_sourceSize;
    QList<QWidget *> m_controls;
    bool m_narrow = false;
};
}
