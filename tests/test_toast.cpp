#include <QtTest>
#include "toast.h"

using namespace eddy;

class TestToast : public QObject {
    Q_OBJECT
private slots:
    void showsMessageText() {
        QWidget parent; parent.resize(200, 200);
        Toast t(&parent);
        t.showMessage("no text detected");
        QCOMPARE(t.text(), QString("no text detected"));
        QVERIFY(t.isVisibleTo(&parent));
    }
    void hiddenByDefault() {
        QWidget parent;
        Toast t(&parent);
        QVERIFY(!t.isVisibleTo(&parent));
    }
};

QTEST_MAIN(TestToast)
#include "test_toast.moc"
