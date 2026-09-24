#pragma once
#include <QVector>
#include "studiodocument.h"

// Editing the source's fragments (studio plan 6.6): pure functions over the
// partition, so the timeline and the bar only forward gestures. An empty list
// means one fragment over the whole source at 1x and is the normal form.
namespace eddy::fragments {

inline constexpr qint64 kMinFragmentMs = 100;

QVector<Fragment> expanded(const QVector<Fragment> &list);
// Back to the empty list when only one plain fragment is left.
QVector<Fragment> simplified(const QVector<Fragment> &list);
int indexAt(const QVector<Fragment> &list, qint64 sourceMs);
qint64 endOf(const QVector<Fragment> &list, int index, qint64 durationMs);
// Splits at `sourceMs`; false when that is within kMinFragmentMs of a seam or an end.
bool split(QVector<Fragment> &list, qint64 sourceMs, qint64 durationMs);
void setSpeed(QVector<Fragment> &list, int index, double speed);
// Cutting the last kept fragment is refused: the output never goes empty.
bool setRemoved(QVector<Fragment> &list, int index, bool removed);
// Merges kept fragment `index` into the kept one before it, which keeps its
// speed. Restoring a cut merges it with equal neighbours on its own.
bool join(QVector<Fragment> &list, int index);

}
