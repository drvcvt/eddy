#pragma once
#include <QVector>
#include <optional>
#include "studio/studiodocument.h"
#include "video/timemap.h"

// Editing the source's fragments (studio plan 6.6): pure functions over the
// partition, so the timeline and the bar only forward gestures. An empty list
// means one fragment over the whole source at 1x and is the normal form.
namespace eddy::fragments {

// How slow and fast a fragment may play; the output is at most 1 / kMinSpeed as long.
inline constexpr double kMinSpeed = 0.25, kMaxSpeed = 4.0;

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
// Cuts [fromMs, toMs) in one go (C1): ends within kMinFragmentMs of a seam or the clip's
// ends move onto it, everything between is cut and melts with cut neighbours into one
// fragment. False when nothing would be cut or nothing kept would remain.
bool cutRange(QVector<Fragment> &list, qint64 fromMs, qint64 toMs, qint64 durationMs);
// C2: every span cut (as cutRange) or played at kMaxSpeed, in one go; ends snap to seams
// within kMinFragmentMs. False, the list unchanged, when nothing kept would remain.
bool applySpans(QVector<Fragment> &list, const QVector<std::pair<qint64, qint64>> &spans, qint64 durationMs, bool cut);
// Merges kept fragment `index` into the kept one before it, which keeps its
// speed. Restoring a cut merges it with equal neighbours on its own.
bool join(QVector<Fragment> &list, int index);

// The clip model (K): the trim and the fragments are one edit. Its pieces are the kept
// stretches inside the trim (TimeMap::pieces), shown as blocks; every block edge moves.
struct Clip {
    qint64 inMs = 0, outMs = 0;
    QVector<Fragment> list;
    bool operator==(const Clip &) const = default;
    QVector<TimePiece> pieces(qint64 durationMs) const { return TimeMap(durationMs, inMs, outMs, list).pieces(); }
};
// Where piece `piece`'s start or end may go: out to the material there is (the kept piece
// before or after it in the source, or the file's ends), in to kMinFragmentMs long.
std::pair<qint64, qint64> startRange(const Clip &clip, int piece, qint64 durationMs);
std::pair<qint64, qint64> endRange(const Clip &clip, int piece, qint64 durationMs);
// Moves an edge, clamped to its range. The outer edges move the trim; inner ones cut the
// source they leave or keep the source they take in, at the piece's speed. A split stays
// a split: taking source back never melts the piece into its neighbour.
Clip moveStart(Clip clip, int piece, qint64 sourceMs, qint64 durationMs);
Clip moveEnd(Clip clip, int piece, qint64 sourceMs, qint64 durationMs);
// P: the pause before piece `piece`, 0 to Fragment::kMaxPauseMs (0 closes it), and whether
// it shows black instead of the frame before held. A pause stays with its piece through
// edge pulls; a split keeps it before the left part; joining or removing drops it.
Clip setPause(Clip clip, int piece, qint64 pauseMs, qint64 durationMs);
Clip setPauseBlack(Clip clip, int piece, bool black, qint64 durationMs);
// Leaves piece `piece` out; the first or last moves the trim onto its neighbour. Empty
// when it is the only piece: the output never goes empty.
std::optional<Clip> removePiece(Clip clip, int piece, qint64 durationMs);
// G: takes the left-out source [fromMs, toMs) back: the trim widens over it and every cut
// fragment touching it is kept again, melting into equal neighbours as setRemoved does.
Clip restore(Clip clip, qint64 fromMs, qint64 toMs, qint64 durationMs);

}
