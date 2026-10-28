#include "video/fragments.h"
#include <algorithm>

namespace eddy::fragments {

namespace {
// A new seam copies the fragment it splits; a pause (P) stays before the piece it was on.
Fragment withoutPause(Fragment f) {
    f.pauseMs = 0;
    f.pauseBlack = false;
    return f;
}
}

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
    Fragment next = withoutPause(parts[i]);
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
        // ... unless a pause (P) stands between the two: then they stay two pieces.
        auto same = [&](int other) {
            const int later = std::max(other, index);
            return other >= 0 && other < parts.size() && !parts[other].removed && parts[other].speed == parts[index].speed
                && parts[later].pauseMs == 0;
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
        Fragment next = withoutPause(parts[i]);
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

bool applySpans(QVector<Fragment> &list, const QVector<std::pair<qint64, qint64>> &spans, qint64 durationMs, bool cut) {
    QVector<Fragment> parts = list;
    for (const auto &[from, to] : spans) {
        if (cut) {
            if (!cutRange(parts, from, to, durationMs)) return false;
            continue;
        }
        // Faster: the same boundaries as a cut, the pieces between played at kMaxSpeed.
        QVector<Fragment> all = expanded(parts);
        QVector<qint64> seams{0, durationMs};
        for (const Fragment &f : std::as_const(all)) seams.append(f.startMs);
        const auto snap = [&](qint64 t) {
            for (const qint64 s : std::as_const(seams)) if (std::abs(t - s) < kMinFragmentMs) return s;
            return t;
        };
        const qint64 a = snap(std::clamp<qint64>(from, 0, durationMs)), b = snap(std::clamp<qint64>(to, 0, durationMs));
        if (b <= a) continue;
        for (const qint64 at : {a, b}) {
            if (at >= durationMs) continue;
            const int i = indexAt(all, at);
            if (all[i].startMs == at) continue;
            Fragment next = withoutPause(all[i]);
            next.startMs = at;
            all.insert(i + 1, next);
        }
        for (Fragment &f : all) if (f.startMs >= a && f.startMs < b && !f.removed) f.speed = kMaxSpeed;
        parts = simplified(all);
    }
    list = parts;
    return true;
}

namespace {

// Neighbouring cuts become one fragment.
QVector<Fragment> meltCuts(const QVector<Fragment> &parts) {
    QVector<Fragment> melted;
    for (const Fragment &f : parts)
        if (melted.isEmpty() || !(melted.last().removed && f.removed)) melted.append(f);
    return melted;
}

// A seam at `at`, copying the fragment it falls in; its index.
int seamAt(QVector<Fragment> &parts, qint64 at) {
    const int i = indexAt(parts, at);
    if (parts[i].startMs == at) return i;
    Fragment next = withoutPause(parts[i]);
    next.startMs = at;
    parts.insert(i + 1, next);
    return i + 1;
}

}

std::pair<qint64, qint64> startRange(const Clip &clip, int piece, qint64 durationMs) {
    const QVector<TimePiece> pieces = clip.pieces(durationMs);
    if (piece < 0 || piece >= pieces.size()) return {0, 0};
    const qint64 lo = piece > 0 ? qint64(pieces[piece - 1].srcEnd) : 0;
    return {lo, std::max(lo, qint64(pieces[piece].srcEnd) - kMinFragmentMs)};
}

std::pair<qint64, qint64> endRange(const Clip &clip, int piece, qint64 durationMs) {
    const QVector<TimePiece> pieces = clip.pieces(durationMs);
    if (piece < 0 || piece >= pieces.size()) return {0, 0};
    const qint64 hi = piece + 1 < pieces.size() ? qint64(pieces[piece + 1].srcStart) : durationMs;
    return {std::min(hi, qint64(pieces[piece].srcStart) + kMinFragmentMs), hi};
}

Clip moveStart(Clip clip, int piece, qint64 sourceMs, qint64 durationMs) {
    const QVector<TimePiece> pieces = clip.pieces(durationMs);
    if (piece < 0 || piece >= pieces.size()) return clip;
    const auto [lo, hi] = startRange(clip, piece, durationMs);
    const qint64 to = std::clamp(sourceMs, lo, hi), from = qint64(pieces[piece].srcStart);
    if (to == from) return clip;
    QVector<Fragment> parts = expanded(clip.list);
    const int own = indexAt(parts, from);   // the fragment the piece plays
    if (to < from) {
        // Taken back: the piece's fragment starts at `to`, swallowing what lay between.
        const int before = parts.size();
        const int first = seamAt(parts, to);
        const int f = own + int(parts.size() - before);   // a new seam sits before the piece's fragment
        Fragment kept = parts[f];
        kept.startMs = to;
        parts.remove(first, f - first + 1);
        parts.insert(first, kept);
    } else if (piece > 0) {
        // Given up: [from, to) is cut and melts with a cut before it; the pause moves on
        // to where the piece now starts.
        const int at = seamAt(parts, to);
        parts[at - 1].removed = true;
        std::swap(parts[at].pauseMs, parts[at - 1].pauseMs);
        std::swap(parts[at].pauseBlack, parts[at - 1].pauseBlack);
    }
    if (piece == 0) clip.inMs = to;
    clip.list = simplified(meltCuts(parts));
    return clip;
}

Clip moveEnd(Clip clip, int piece, qint64 sourceMs, qint64 durationMs) {
    const QVector<TimePiece> pieces = clip.pieces(durationMs);
    if (piece < 0 || piece >= pieces.size()) return clip;
    const auto [lo, hi] = endRange(clip, piece, durationMs);
    const qint64 to = std::clamp(sourceMs, lo, hi), from = qint64(pieces[piece].srcEnd);
    if (to == from) return clip;
    QVector<Fragment> parts = expanded(clip.list);
    const int own = indexAt(parts, qint64(pieces[piece].srcStart));
    const bool last = piece + 1 == pieces.size();
    if (to > from) {
        // Taken back: every seam between the piece and `to` goes, so its fragment runs on.
        const int stop = to < durationMs ? seamAt(parts, to) : parts.size();
        parts.remove(own + 1, stop - own - 1);
    } else if (!last) {
        // Given up: [to, from) is cut and melts with the cut after it.
        parts[seamAt(parts, to)].removed = true;
    }
    if (last) clip.outMs = to;
    clip.list = simplified(meltCuts(parts));
    return clip;
}

Clip setPause(Clip clip, int piece, qint64 pauseMs, qint64 durationMs) {
    const QVector<TimePiece> pieces = clip.pieces(durationMs);
    if (piece < 0 || piece >= pieces.size()) return clip;
    QVector<Fragment> parts = expanded(clip.list);
    Fragment &f = parts[indexAt(parts, qint64(pieces[piece].srcStart))];
    f.pauseMs = std::clamp<qint64>(pauseMs, 0, Fragment::kMaxPauseMs);
    if (f.pauseMs == 0) f.pauseBlack = false;
    clip.list = simplified(parts);
    return clip;
}

Clip setPauseBlack(Clip clip, int piece, bool black, qint64 durationMs) {
    const QVector<TimePiece> pieces = clip.pieces(durationMs);
    if (piece < 0 || piece >= pieces.size() || pieces[piece].pauseBefore <= 0) return clip;
    QVector<Fragment> parts = expanded(clip.list);
    parts[indexAt(parts, qint64(pieces[piece].srcStart))].pauseBlack = black;
    clip.list = simplified(parts);
    return clip;
}

std::optional<Clip> removePiece(Clip clip, int piece, qint64 durationMs) {
    const QVector<TimePiece> pieces = clip.pieces(durationMs);
    if (piece < 0 || piece >= pieces.size() || pieces.size() < 2) return std::nullopt;
    if (piece == 0) {
        clip.inMs = qint64(pieces[1].srcStart);
    } else if (piece + 1 == pieces.size()) {
        clip.outMs = qint64(pieces[piece - 1].srcEnd);
    } else {
        QVector<Fragment> parts = expanded(clip.list);
        parts[indexAt(parts, qint64(pieces[piece].srcStart))].removed = true;
        clip.list = simplified(meltCuts(parts));
    }
    return clip;
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
