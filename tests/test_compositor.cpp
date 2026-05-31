#include <QtTest>
#include "compositor.h"
using namespace eddy;
class TestCompositor : public QObject {
    Q_OBJECT
private slots:
    void rulesIncludeFloatAndNoanim() {
        QStringList r = hyprlandRules("eddy");
        QVERIFY(r.filter("float").size() >= 1);
        QVERIFY(r.filter("noanim").size() >= 1);
        QVERIFY(r.filter("class:^(eddy)$").size() >= 1);
    }
};
QTEST_GUILESS_MAIN(TestCompositor)
#include "test_compositor.moc"
