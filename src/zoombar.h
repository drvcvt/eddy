#pragma once
#include <QWidget>
#include "studiodocument.h"
class QToolButton;
namespace eddy {

// The selected zoom's floating bar at the bottom of the canvas (Q2 = A):
// zoom level, motion, remove. Follow Cursor joins it with S5.
class ZoomBar : public QWidget {
    Q_OBJECT
public:
    explicit ZoomBar(QWidget *parent = nullptr);
    void setZoom(const ZoomSegment &zoom);
    // Follow Cursor needs Boltsnap's pointer track beside the video.
    void setCursorAvailable(bool available);
    void refreshTheme();
signals:
    void scaleChosen(double scale);
    void motionChosen(ZoomSegment::Motion motion);
    void targetChosen(ZoomSegment::Target target);
    void removeRequested();
private:
    QToolButton *m_scale = nullptr;
    QToolButton *m_motion = nullptr;
    QToolButton *m_point = nullptr;
    QToolButton *m_cursor = nullptr;
};

}
