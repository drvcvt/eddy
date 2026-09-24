#pragma once
#include <QColor>
#include <QIcon>
#include <QPolygonF>
#include "studiodocument.h"

namespace eddy {

// The Motion symbols (studio plan 11, Q4b and Q4d): the step response the
// camera really uses, so the symbols change with the springs. 0.25 s before
// the step and 1.2 s after it, in the icon set's 24-unit box with a 20-unit
// live area and its 2.8 stroke.
QPolygonF motionCurve(ZoomSegment::Motion motion);
QIcon motionIcon(ZoomSegment::Motion motion, const QColor &ink, int size);
QString motionName(ZoomSegment::Motion motion);

}
