#include <QtTest>
#include "editorwindow.h"
using namespace eddy;
class TestEditorWindow : public QObject {
    Q_OBJECT
private slots:
    void buildsAndExportsCompositeSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QImage out = w.exportComposite();
        QCOMPARE(out.size(), QSize(64,48));
    }
};
QTEST_MAIN(TestEditorWindow)
#include "test_editorwindow.moc"
