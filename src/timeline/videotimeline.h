#pragma once
#include "app/theme.h"
#include <QWidget>
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <functional>
#include "studio/studiodocument.h"
#include "video/timemap.h"
#include "video/fragments.h"

namespace eddy {

class AudioWaveformProvider;
class Spring;
class SpringLevels;

class VideoTimeline : public QWidget {
    Q_OBJECT
    enum class Drag { None, Edge, Move, Seek, Mark, ZoomAdd, ZoomMove, ZoomStart, ZoomEnd, MaskMove, MaskStart, MaskEnd };
public:
    // Geometry (docs/design.md): a caption-high ruler, the film strip, then each
    // optional lane below it, all kS1 apart.
    static constexpr qreal kInset = 6;                               // a grip's width, inside its piece; also the track's side inset
    static constexpr qreal kGripReach = kInset / 2 + 4;             // from a grip's middle: the grip and 4 px of slop
    static constexpr qreal kGap = theme::kS1;                        // between two blocks (K)
    static constexpr qreal kRulerH = 16;                             // a 12 px caption and its line
    static constexpr qreal kTrackTop = kRulerH + theme::kS1;
    static constexpr qreal kTrackH = 32;
    static constexpr qreal kLaneH = 28, kMaskH = 20, kWaveH = kMaskH;   // the waveform carries no text
    // Each lane brings the kS1 above it; nothing empty stays under the last one.
    static constexpr int kBaseH = int(kTrackTop + kTrackH);
    static constexpr int kLaneStep = theme::kS1 + int(kLaneH), kMaskStep = theme::kS1 + int(kMaskH);
    static constexpr int kWaveStep = theme::kS1 + int(kWaveH);
    explicit VideoTimeline(QWidget *parent = nullptr);

    void setDuration(qint64 durationMs);
    void setMinimumRange(qint64 durationMs);
    void setPosition(qint64 positionMs);
    void setTrimRange(qint64 inMs, qint64 outMs);
    // The clip model (K): the trim and the fragments together. The strip shows the output:
    // every kept piece a block, everything left out (ends and cuts alike) gone. A change
    // glides the blocks from where they were (motion::kBase).
    void setClip(qint64 inMs, qint64 outMs, const QVector<Fragment> &fragments);
    fragments::Clip clip() const { return {m_in, m_out, m_fragments}; }
    // Blocks as shown, in widget pixels (gaps and a running glide included).
    QVector<QRectF> pieceRects() const;
    // P: the pause before piece `piece` as shown, empty without one.
    QRectF pauseRect(int piece) const;
    bool movingPiece() const { return m_drag == Drag::Move; }
    // P: the playhead inside a pause, at output time `outMs` (it has no source time of its
    // own); setPosition() takes it out again.
    void setPausePosition(double outMs);
    double pausePosition() const { return m_pauseHead; }   // -1 outside a pause
    void setThumbnail(qint64 timeMs, const QImage &image);
    void zoomAt(qreal factor, qint64 anchorMs);
    void panBy(qint64 deltaMs);
    void fitClip();
    void cancelInteraction();
    // Source times at the view's edges.
    qint64 visibleStart() const { return source(m_viewStart); }
    qint64 visibleEnd() const { return source(m_viewEnd); }
    // Pans so `sourceMs` is in view.
    void ensureVisible(qint64 sourceMs);
    // Fragments (studio plan 6.6, Q5 = B): the timeline shows the edited
    // time, cuts collapse to a notch in the ruler and 2x takes half the width.
    // Every public time stays source time.
    void setFragments(const QVector<Fragment> &fragments);
    void setSelectedFragment(int index);   // -1: none; the piece playing that fragment is framed
    // Mask lane (studio plan 6.7, E4): one block per redaction or spotlight
    // with a time window, under the zoom lane.
    struct MaskBlock {
        quintptr key;          // identifies the item for the signals
        qint64 fromMs, toMs;
        QString label;
        bool selected = false;
        bool operator==(const MaskBlock &) const = default;
    };
    void setMasks(const QVector<MaskBlock> &masks);
    QRectF maskLaneRect() const;
    bool maskDragging() const {
        return m_drag == Drag::MaskMove || m_drag == Drag::MaskStart || m_drag == Drag::MaskEnd;
    }
    // Ruler ticks as (x, edited ms).
    QVector<QPair<qreal, qint64>> rulerTicks() const;
    QVector<qint64> thumbnailTimes() const;
    QImage thumbnailNear(qint64 time, qint64 *sampleTime) const;
    bool interacting() const { return m_drag != Drag::None; }
    // A range marked to cut (C1): Shift-drag or Shift-click on the strip, or Shift+I and
    // Shift+O at the playhead. Source time, not part of the document; the window cuts it.
    bool hasMark() const;
    std::pair<qint64, qint64> markRange() const;   // sorted, inside the trim; {0, 0} without one
    bool marking() const { return m_drag == Drag::Mark; }
    bool markOpen() const { return m_markOpen; }   // Shift+I given, Shift+O not yet
    void setMark(qint64 anchorMs, qint64 headMs);
    void markFromPlayhead();   // an open mark: its head follows the playhead
    void markToPlayhead();
    void clearMark();
    bool trimming() const { return m_drag == Drag::Edge; }

    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }
    qint64 trimIn() const { return m_in; }
    qint64 trimOut() const { return m_out; }

    // Waveform lane (21.09. plan 4, E8): 20 px right under the film strip,
    // for videos with sound. Clicks scrub like the film strip. Hiding it is a
    // view setting from the context menu, not an edit.
    void setWaveform(AudioWaveformProvider *waveform);
    void setWaveformVisible(bool visible);
    // A silent track, once read to the end, folds its lane away.
    bool waveformShown() const;
    AudioWaveformProvider *waveform() const;
    QRectF waveformRect() const;

    // Zoom lane (studio plan 6.1, Q1 = C): a 28 px row under the film strip on
    // the same time axis, shown while Studio is on or zooms exist.
    void setZoomLaneVisible(bool visible);
    bool zoomLaneVisible() const { return m_laneVisible; }
    // `level` is the camera's zoom at a source time, 1 when it shows everything.
    void setZooms(const QVector<ZoomSegment> &zooms, std::function<double(qint64)> level = {});
    QVector<ZoomSegment> zooms() const { return m_zooms; }
    void setSelectedZoom(quint32 id);   // 0: none
    quint32 selectedZoom() const { return m_selectedZoom; }
    QRectF zoomLaneRect() const;
    bool zoomDragging() const {
        return m_drag == Drag::ZoomMove || m_drag == Drag::ZoomStart || m_drag == Drag::ZoomEnd;
    }

signals:
    void seekRequested(qint64 positionMs);
    // A piece edge was pulled (K): one edit of the trim and the fragments.
    void clipEdited(const fragments::Clip &before, const fragments::Clip &after, int piece);
    void interactionStarted(bool trimming);
    void interactionFinished(bool cancelled);
    void viewRangeChanged();
    void hoverRequested(qint64 timeMs, QPoint position);
    void hoverLeft();
    void zoomAddRequested(qint64 timeMs);
    void zoomSelected(quint32 id);
    void zoomsPreviewed(const QVector<ZoomSegment> &zooms);
    void zoomsEdited(const QVector<ZoomSegment> &before, const QVector<ZoomSegment> &after);
    void zoomMenuRequested(quint32 id, QPoint globalPos);
    void cutClicked(int fragment);
    // A press and release on the strip or waveform within 3 px: a click, not a scrub.
    void stripClicked(qint64 positionMs);
    void markChanged();
    void pausesRequested(bool cut);   // cut them, or play them at 4x
    void maskSelected(quintptr key);
    void maskWindowPreviewed(quintptr key, qint64 fromMs, qint64 toMs);
    void maskWindowEdited(quintptr key, qint64 fromMs, qint64 toMs, qint64 beforeFrom, qint64 beforeTo);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    QRectF trackRect() const;
    quint32 zoomAt(QPointF pos, Drag *part) const;
    int cutAt(QPointF pos) const;
    int maskAt(QPointF pos, Drag *part) const;
    void moveMaskDrag(qreal x);
    void updateHeight();
    qreal belowWaveform() const;   // the bottom of the film strip or the waveform under it
    void paintWaveform(QPainter &painter, const std::function<QColor(qreal)> &ink);
    void paintZoomLane(QPainter &painter, const std::function<QColor(qreal)> &ink);
    void paintMaskLane(QPainter &painter, const std::function<QColor(qreal)> &ink);
    void contextMenu(QPoint pos);
    bool inZoomLane(QPointF pos) const;
    double edited(qint64 sourceMs) const;
    qint64 source(double editedMs) const;
    qint64 editedDuration() const;
    void rebuildAxis();
    void moveZoomDrag(qreal x);
    qreal xForTime(qint64 timeMs) const;
    qint64 timeForX(qreal x) const;
    void setViewRange(qint64 startMs, qint64 endMs);
    void moveDrag(qreal x, Qt::KeyboardModifiers modifiers);
    // Piece edges (videotimeline_clips.cpp).
    qreal xForOutput(double outMs) const;
    bool edgeAt(QPointF pos, int *piece, bool *end) const;
    void beginEdgeDrag(int piece, bool end, qreal x, Qt::KeyboardModifiers modifiers);
    void moveEdgeDrag(qreal x);
    int selectedPiece() const;
    int pieceAt(QPointF pos) const;    // the block under `pos` (its body), -1 for none
    int pauseAt(QPointF pos) const;    // the pause under `pos`: the piece after it, -1 for none
    void beginMove(int piece, qreal x);
    void moveMove(qreal x);
    void editPause(int piece, std::optional<qint64> pauseMs, std::optional<bool> black);
    int m_movePiece = -1;
    double m_movePause = 0, m_moveNow = 0;   // the moved piece's pause at the press, and now
    double m_pauseHead = -1;
    bool notchShown(int fragment) const;   // a cut inside the output, not at its ends
    qreal notchX(int fragment) const;      // where a cut sits: the end of what is kept before it
    int m_edgePiece = -1, m_hoverEdge = -1;   // hover: piece * 2 + (end ? 1 : 0)
    bool m_edgeEnd = false;
    qint64 m_edgeSource = 0, m_edgeNow = 0;   // the edge's source time at the press, and now
    double m_edgeOut = 0;                      // its output time at the press
    fragments::Clip m_clipBefore, m_clipPreview;
    // The glide after an edit: each piece's start and end, in px, from where they were.
    QVector<std::pair<qreal, qreal>> m_ripple;
    Spring *m_rippleGlide = nullptr;

    qint64 m_duration = 0;
    qint64 m_position = 0;
    qint64 m_in = 0;
    qint64 m_out = 0;
    qint64 m_minimumRange = 1;
    qint64 m_viewStart = 0, m_viewEnd = 0;
    qint64 m_beforePosition = 0;
    qreal m_dragX = 0, m_dragTime = 0;
    bool m_fine = false;
    qreal m_pointerX = 0;
    Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
    QTimer m_edgePan;
    QHash<qint64, QImage> m_thumbnails;
    Drag m_drag = Drag::None;
    bool m_laneVisible = false;
    Spring *m_height = nullptr;   // lanes come and go on motion::kSlow
    qreal m_pressX = 0;           // where a strip press began, for click versus scrub
    bool m_pressMoved = false;
    qint64 snapMark(qint64 timeMs) const;
    void showMark();
    qint64 m_markAnchor = -1, m_markHead = -1, m_markClickAnchor = -1;
    qint64 m_zoomAddAt = 0;   // a press on the empty zoom lane adds there on a still release
    // Every small state eases on motion::kFast (C9): grips, focus, chosen zooms and masks,
    // lit notches, the fragments stepping back. Aimed while painting, read for the inks.
    SpringLevels *m_states = nullptr;
    void syncStates();
    QString hintAt(QPointF pos) const;
    // Keys for the eased states: one space per kind, the item's own id below.
    static constexpr quint64 kFocus = 3;
    static constexpr quint64 kZoom = 1, kMask = 2, kNotch = 3, kEdge = 4, kPicked = 5;
    static constexpr quint64 stateKey(quint64 kind, quint64 id) {
        return (kind << 56) | (id & ((quint64(1) << 56) - 1));
    }
    // `+`, `-`, `0` and the menu glide the view on motion::kBase; the wheel stays direct.
    void glideView(const std::function<void()> &change);
    Spring *m_glide = nullptr;
    std::pair<qint64, qint64> m_glideFrom{0, 0}, m_glideTo{0, 0};
    bool m_gliding = false;
    void focusInEvent(QFocusEvent *event) override;
    bool m_markOpen = false;
    struct { qint64 anchor = -1, head = -1; bool open = false; } m_markBefore;   // Esc mid-drag
    std::pair<qint64, qint64> m_markShown{0, 0};   // painted, also while it fades out
    Spring *m_markFade = nullptr;
    // One brightness step (the ladder's bg -> raise1 is about 0.03 L): the marked range is
    // lifted towards the text colour, no line, no edge.
    static constexpr qreal kMarkLift = 0.10;
    QVector<ZoomSegment> m_zooms, m_zoomsBefore;
    std::function<double(qint64)> m_zoomLevel;
    quint32 m_selectedZoom = 0, m_dragZoom = 0;
    qint64 m_grabOffset = 0;
    bool m_zoomMoved = false;
    QVector<Fragment> m_fragments;
    TimeMap m_axis;                 // source -> output time: the trim and the fragments
    int m_selectedFragment = -1;
    int m_hoverCut = -1;
    QVector<MaskBlock> m_masks;
    int m_dragMask = -1;
    QPointer<AudioWaveformProvider> m_waveform;
    bool m_waveformVisible = true;
    QImage m_waveCache;             // the lane, drawn once per view and axis
    size_t m_waveKey = 0;
    quint64 m_waveRevision = 0;     // new peaks or a new axis
    MaskBlock m_maskBefore{};
};

}
