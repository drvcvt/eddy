// The playhead and the clock on the keyboard (C3, C4): frame and second steps through
// cuts, Home/End, edges, and what keeps the keys when something else has them.
#include "editortest.h"

namespace {

MediaDocument clip(qint64 durationMs = 10000) {
    MediaDocument doc;
    doc.kind = MediaKind::Video;
    doc.path = QStringLiteral("/tmp/nonexistent-videoclock.mp4");
    doc.video = {QSize(160, 90), durationMs, 25.0};
    return doc;
}

Fragment part(qint64 start, double speed = 1, bool removed = false) {
    Fragment f;
    f.startMs = start;
    f.speed = speed;
    f.removed = removed;
    return f;
}

void withFragments(EditorWindow &w, const QVector<Fragment> &fragments) {
    StudioDocument doc = w.studioDocument();
    doc.fragments = fragments;
    w.setStudioDocument(doc);
}

}

class TestVideoClock : public QObject {
    Q_OBJECT
private slots:
    void arrowsStepAFrameOrASecond() {
        Config cfg; cfg.animations = false;
        EditorWindow w(clip(), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        timeline->setPosition(1000);
        QTest::keyClick(&w, Qt::Key_Right);
        QCOMPARE(timeline->position(), 1040);
        QTest::keyClick(&w, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(timeline->position(), 2040);
        QTest::keyClick(&w, Qt::Key_Left);
        QCOMPARE(timeline->position(), 2000);
        QCOMPARE(undo->count(), steps);   // the playhead is no edit
    }
    void stepsSkipCutsAndFollowSpeed() {
        Config cfg; cfg.animations = false;
        EditorWindow w(clip(), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        withFragments(w, {part(0), part(2000, 1, true), part(5000)});
        timeline->setPosition(1960);
        QTest::keyClick(&w, Qt::Key_Right);
        QCOMPARE(timeline->position(), 5000);
        QTest::keyClick(&w, Qt::Key_Left);
        QCOMPARE(timeline->position(), 1960);
        withFragments(w, {part(0, 2.0)});
        timeline->setPosition(0);
        QTest::keyClick(&w, Qt::Key_Right, Qt::ShiftModifier);   // a second as it plays, at 2x
        QCOMPARE(timeline->position(), 2000);
    }
    void homeEndAndEdges() {
        Config cfg; cfg.animations = false;
        EditorWindow w(clip(), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        emit timeline->trimCommitted(1000, 8000);
        QTest::keyClick(&w, Qt::Key_Home);
        QCOMPARE(timeline->position(), 1000);
        QTest::keyClick(&w, Qt::Key_End);
        QCOMPARE(timeline->position(), 7960);   // the last frame of the range
        emit timeline->trimCommitted(0, 10000);
        StudioDocument doc = w.studioDocument();
        ZoomSegment zoom;
        zoom.id = 1;
        zoom.startMs = 3000;
        zoom.endMs = 5000;
        doc.zooms = {zoom};
        w.setStudioDocument(doc);
        timeline->setPosition(0);
        QTest::keyClick(&w, Qt::Key_Down);
        QCOMPARE(timeline->position(), 3000);
        QTest::keyClick(&w, Qt::Key_Down);
        QCOMPARE(timeline->position(), 5000);
        QTest::keyClick(&w, Qt::Key_Down);
        QCOMPARE(timeline->position(), 9960);   // the clip's last frame
        QTest::keyClick(&w, Qt::Key_Up);
        QCOMPARE(timeline->position(), 5000);
    }
    void aSelectionOrAFieldKeepsItsArrows() {
        Config cfg; cfg.animations = false;
        EditorWindow w(clip(), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *rect = new RectItem(QRectF(4, 4, 20, 20));
        rect->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        w.findChild<QGraphicsScene *>()->addItem(rect);
        rect->setSelected(true);
        timeline->setPosition(1000);
        QTest::keyClick(&w, Qt::Key_Right);
        QCOMPARE(rect->pos(), QPointF(1, 0));   // nudged
        QCOMPARE(timeline->position(), 1000);
        rect->setSelected(false);
        auto *field = w.findChild<QLineEdit *>(QStringLiteral("TrimInTime"));
        field->setFocus();
        field->setCursorPosition(3);
        QTest::keyClick(field, Qt::Key_Left);
        QCOMPARE(field->cursorPosition(), 2);
        QCOMPARE(timeline->position(), 1000);
    }
    void aHeldArrowRunsAndSeeksOnRelease() {
        Config cfg; cfg.animations = false;
        EditorWindow w(clip(), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        timeline->setPosition(0);
        for (int i = 0; i < 3; ++i) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, QString(), i > 0);
            QApplication::sendEvent(&w, &press);
        }
        QCOMPARE(timeline->position(), 120);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(&w, &release);
        QCOMPARE(timeline->position(), 120);
    }
    // C4: one clock. A cut 2..5 s folds 3 s out of the ruler and every time shown.
    void everyTimeReadsTheRulersClock() {
        Config cfg; cfg.animations = false;
        EditorWindow w(clip(), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        withFragments(w, {part(0), part(2000, 1, true), part(5000)});
        auto *start = w.findChild<QLineEdit *>(QStringLiteral("TrimInTime"));
        auto *end = w.findChild<QLineEdit *>(QStringLiteral("TrimOutTime"));
        auto *length = w.findChild<QLineEdit *>(QStringLiteral("TrimDuration"));
        timeline->setPosition(6000);
        QTest::keyClick(&w, Qt::Key_I);
        QCOMPARE(start->text(), QStringLiteral("0:03.000"));
        QCOMPARE(end->text(), QStringLiteral("0:07.000"));
        QCOMPARE(length->text(), QStringLiteral("0:04.000"));
        QTest::keyClick(&w, Qt::Key_L);
        QCOMPARE(w.findChild<QLabel *>(QStringLiteral("PlaybackTime"))->text(), QStringLiteral("0:03.040"));
        const auto type = [](QLineEdit *field, const QString &text) {
            field->setFocus();
            field->selectAll();
            QTest::keyClicks(field, text);
            QTest::keyClick(field, Qt::Key_Return);
        };
        int steps = undo->count();
        type(end, QStringLiteral("5.000"));
        QCOMPARE(timeline->trimOut(), 8000);
        QCOMPARE(length->text(), QStringLiteral("0:02.000"));
        QCOMPARE(undo->count(), steps + 1);
        type(length, QStringLiteral("1.000"));   // a length sets End
        QCOMPARE(timeline->trimOut(), 7000);
        QCOMPARE(end->text(), QStringLiteral("0:04.000"));
        QCOMPARE(undo->count(), steps + 2);
        type(length, QStringLiteral("9.000"));
        QCOMPARE(w.findChild<Toast *>()->text(), QStringLiteral("Enter a length that fits after Start"));
        QCOMPARE(timeline->trimOut(), 7000);
        // Speed changes the numbers, never the stored frames.
        withFragments(w, {part(0), part(2000, 1, true), part(5000, 2.0)});
        QCOMPARE(timeline->trimIn(), 6000);
        QCOMPARE(start->text(), QStringLiteral("0:02.500"));
        QCOMPARE(length->text(), QStringLiteral("0:00.500"));
        // One box for all, wide enough for the slowest output.
        QCOMPARE(start->width(), end->width());
        QCOMPARE(start->width(), length->width());
    }
    void arrowsInATimeFieldStepItAsOneEdit() {
        Config cfg; cfg.animations = false;
        EditorWindow w(clip(), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        withFragments(w, {part(0), part(2000, 1, true), part(5000)});
        emit timeline->trimCommitted(6000, 10000);
        auto *start = w.findChild<QLineEdit *>(QStringLiteral("TrimInTime"));
        const int steps = undo->count();
        start->setFocus();
        QTest::keyClick(start, Qt::Key_Up);
        QTest::keyClick(start, Qt::Key_Up);
        QCOMPARE(timeline->trimIn(), 6080);
        QCOMPARE(start->text(), QStringLiteral("0:03.080"));
        QCOMPARE(timeline->position(), 6080);   // the bound's frame shows
        QTest::keyClick(start, Qt::Key_Return);
        QCOMPARE(undo->count(), steps + 1);       // two steps, one undo
        QTest::keyClick(start, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(start->text(), QStringLiteral("0:04.080"));
        QCOMPARE(undo->count(), steps + 2);       // a new run after Enter
        emit timeline->trimCommitted(5000, 10000);
        QTest::keyClick(start, Qt::Key_Down);     // over the cut
        QCOMPARE(timeline->trimIn(), 1960);
        QCOMPARE(start->text(), QStringLiteral("0:01.960"));
    }
};

QTEST_MAIN(TestVideoClock)
#include "test_videoclock.moc"
