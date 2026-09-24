#include <QtTest>
#include <QAbstractButton>
#include <QLabel>
#include <QToolButton>
#include "exportpanel.h"

using namespace eddy;

static QToolButton *segment(ExportPanel &panel, const QString &row, const QString &text) {
    for (auto *b : panel.findChildren<QToolButton *>(QStringLiteral("ExportSegment")))
        if (b->accessibleName() == row + QLatin1Char(' ') + text) return b;
    return nullptr;
}

class TestExportPanel : public QObject {
    Q_OBJECT
private slots:
    void presetsSetTheRowsAndARowLeavesThePreset() {
        ExportPanel panel;
        panel.setOutput(QSize(2058, 1190), 23400, QStringLiteral("/a/clip.mov"));
        QSignalSpy changed(&panel, &ExportPanel::settingsChanged);
        QVERIFY(segment(panel, "Preset", "Original")->isChecked());
        QCOMPARE(panel.findChild<QToolButton *>("ExportSave")->text(), QStringLiteral("Save MOV"));
        for (auto *b : panel.findChildren<QToolButton *>(QStringLiteral("ExportSegment")))
            if (b->accessibleName().startsWith(QStringLiteral("Format"))) QVERIFY(!b->isChecked());   // MOV
        panel.setOutput(QSize(2058, 1190), 23400, QStringLiteral("/a/clip.mp4"));
        QVERIFY(segment(panel, "Format", "MP4")->isChecked());
        segment(panel, "Preset", "GIF")->click();
        QCOMPARE(panel.settings(), exportPreset(ExportPreset::Gif));
        QVERIFY(segment(panel, "Format", "GIF")->isChecked());
        QVERIFY(segment(panel, "Size", "480")->isChecked());
        QVERIFY(segment(panel, "Frames", "15")->isChecked());
        QCOMPARE(panel.findChild<QLabel *>("ExportSummary")->text(), QStringLiteral("830 × 480   0:23"));
        QCOMPARE(panel.findChild<QToolButton *>("ExportSave")->text(), QStringLiteral("Save GIF"));
        segment(panel, "Size", "720")->click();
        QCOMPARE(panel.settings().shortSide, 720);
        for (auto *b : panel.findChildren<QToolButton *>(QStringLiteral("ExportSegment")))
            if (b->accessibleName().startsWith(QStringLiteral("Preset"))) QVERIFY(!b->isChecked());
        QCOMPARE(changed.count(), 2);
        // The same clock as the playback bar: hours when there are any, no rounding up.
        panel.setOutput(QSize(640, 360), 3661900, QStringLiteral("/a/clip.mp4"));
        QVERIFY(panel.findChild<QLabel *>("ExportSummary")->text().endsWith(QStringLiteral("1:01:01")));
        QSignalSpy save(&panel, &ExportPanel::saveRequested);
        panel.findChild<QToolButton *>("ExportSave")->click();
        QCOMPARE(save.count(), 1);
    }
};

QTEST_MAIN(TestExportPanel)
#include "test_exportpanel.moc"
