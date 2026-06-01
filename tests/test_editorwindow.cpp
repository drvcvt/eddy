#include <QtTest>
#include "editorwindow.h"
#include "redactbar.h"
#include "toast.h"
#include "dragpill.h"
using namespace eddy;
class TestEditorWindow : public QObject {
    Q_OBJECT
private slots:
    void hasRedactBarAndToast() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.findChild<RedactBar *>() != nullptr);   // mode-bar constructed
        QVERIFY(w.findChild<Toast *>() != nullptr);        // toast constructed
        QVERIFY(w.findChild<RedactBar *>()->isHidden());   // hidden until a redact is selected
    }
    void buildsAndExportsCompositeSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QImage out = w.exportComposite();
        QCOMPARE(out.size(), QSize(64,48));
    }
    void hasMinimumSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.minimumWidth() > 0);
        QVERIFY(w.minimumHeight() > 0);
    }
    void hasDragOutPill() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *pill = w.findChild<DragPill *>();
        QVERIFY(pill != nullptr);            // pill exists and is visible (not hidden like the bar/toast)
        QVERIFY(!pill->isHidden());
    }
};
QTEST_MAIN(TestEditorWindow)
#include "test_editorwindow.moc"
