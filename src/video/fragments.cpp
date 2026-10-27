#include "video/fragments.h"
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
    parts[index].speed = std::clamp(speed, kMinSpeed, kMaxSpeed);
    list = simplified(parts);
}

bool setRemoved(QVector<Fragment> &list, int index, bool removed) {
    QVector<Fragment> parts = expanded(list);
    if (index < 0 || index >= parts.size()) return false;
    if (removed && std::count_if(parts.cbegin(), parts.cend(), [](const Fragment &f) { return !f.removed; }) <= 1
        && !parts[index].removed)
        return false;
    parts[index].removed = removed;
    if (!removed) {
        // A restored fragment melts into equal neighbours again (studio plan 6.6).
        auto same = [&](int other) {
            return other >= 0 && other < parts.size() && !parts[other].removed && parts[other].speed == parts[index].speed;
        };
        if (same(index + 1)) parts.remove(index + 1);
        if (same(index - 1)) parts.remove(index);
    }
    list = simplified(parts);
    return true;
}

bool cutRange(QVector<Fragment> &list, qint64 fromMs, qint64 toMs, qint64 durationMs) {
    QVector<Fragment> parts = expanded(list);
    qint64 from = std::clamp<qint64>(std::min(fromMs, toMs), 0, durationMs);
    qint64 to = std::clamp<qint64>(std::max(fromMs, toMs), 0, durationMs);
    // No sliver under kMinFragmentMs is left next to a seam or an end: what shows is what goes.
    QVector<qint64> seams{0, durationMs};
    for (const Fragment &f : std::as_const(parts)) seams.append(f.startMs);
    const auto snap = [&](qint64 t) {
        for (const qint64 s : std::as_const(seams))
            if (std::abs(t - s) < kMinFragmentMs) return s;
        return t;
    };
    from = snap(from);
    to = snap(to);
    if (to <= from) return false;
    // Boundaries copy the piece they fall in (split() refuses short pieces on purpose).
    for (const qint64 at : {from, to}) {
        if (at >= durationMs) continue;
        const int i = indexAt(parts, at);
        if (parts[i].startMs == at) continue;
        Fragment next = parts[i];
        next.startMs = at;
        parts.insert(i + 1, next);
    }
    for (Fragment &f : parts)
        if (f.startMs >= from && f.startMs < to) f.removed = true;
    QVector<Fragment> melted;
    for (const Fragment &f : std::as_const(parts))
        if (melted.isEmpty() || !(melted.last().removed && f.removed)) melted.append(f);
    if (std::none_of(melted.cbegin(), melted.cend(), [](const Fragment &f) { return !f.removed; })) return false;
    list = simplified(melted);
    return true;
}

bool join(QVector<Fragment> &list, int index) {
    QVector<Fragment> parts = expanded(list);
    // Only two kept fragments join; joining into a cut would cut what was kept.
    if (index <= 0 || index >= parts.size() || parts[index].removed || parts[index - 1].removed) return false;
    parts.remove(index);
    list = simplified(parts);
    return true;
}

}
