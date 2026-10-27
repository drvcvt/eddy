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
};

QTEST_MAIN(TestVideoClock)
#include "test_videoclock.moc"
