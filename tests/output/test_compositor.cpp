#include <QtTest>
#include "output/compositor.h"
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
    void batchCarriesEveryRuleAsOneCommandEach() {
        const QStringList commands = hyprlandBatch("eddy").split(" ; ");
        const QStringList rules = hyprlandRules("eddy");
        QCOMPARE(commands.size(), rules.size());
        for (int i = 0; i < rules.size(); ++i)
            QCOMPARE(commands[i], QStringLiteral("keyword windowrulev2 ") + rules[i]);
    }
};
QTEST_GUILESS_MAIN(TestCompositor)
#include "test_compositor.moc"
