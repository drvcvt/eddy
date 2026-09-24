#include "fragments.h"
#include <algorithm>

namespace eddy::fragments {

QVector<Fragment> expanded(const QVector<Fragment> &list) {
    return list.isEmpty() ? QVector<Fragment>{Fragment()} : list;
}

QVector<Fragment> simplified(const QVector<Fragment> &list) {
    return list.size() == 1 && list.first() == Fragment() ? QVector<Fragment>() : list;
}

int indexAt(const QVector<Fragment> &list, qint64 sourceMs) {
    const QVector<Fragment> parts = expanded(list);
    int index = 0;
    for (int i = 0; i < parts.size(); ++i)
        if (parts[i].startMs <= sourceMs) index = i;
    return index;
}

qint64 endOf(const QVector<Fragment> &list, int index, qint64 durationMs) {
    const QVector<Fragment> parts = expanded(list);
    return index + 1 < parts.size() ? parts[index + 1].startMs : durationMs;
}

bool split(QVector<Fragment> &list, qint64 sourceMs, qint64 durationMs) {
    QVector<Fragment> parts = expanded(list);
    const int i = indexAt(parts, sourceMs);
    if (sourceMs - parts[i].startMs < kMinFragmentMs || endOf(parts, i, durationMs) - sourceMs < kMinFragmentMs)
        return false;
    Fragment next = parts[i];
    next.startMs = sourceMs;
    parts.insert(i + 1, next);
    list = parts;
    return true;
}

void setSpeed(QVector<Fragment> &list, int index, double speed) {
    QVector<Fragment> parts = expanded(list);
    if (index < 0 || index >= parts.size()) return;
    parts[index].speed = std::clamp(speed, 0.25, 4.0);
    list = simplified(parts);
}

bool setRemoved(QVector<Fragment> &list, int index, bool removed) {
    QVector<Fragment> parts = expanded(list);
    if (index < 0 || index >= parts.size()) return false;
    if (removed && std::count_if(parts.cbegin(), parts.cend(), [](const Fragment &f) { return !f.removed; }) <= 1
        && !parts[index].removed)
        return false;
    parts[index].removed = removed;
    list = simplified(parts);
    return true;
}

bool join(QVector<Fragment> &list, int index) {
    QVector<Fragment> parts = expanded(list);
    if (index <= 0 || index >= parts.size()) return false;
    parts.remove(index);
    list = simplified(parts);
    return true;
}

}
