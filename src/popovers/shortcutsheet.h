#pragma once
#include <QString>
#include <QVector>
#include <QWidget>

namespace eddy {

// Every shortcut on one sheet (H1): `?`, F1 or the keyboard button in the top bar open it.
// The rows come from one table, so the sheet and the keys cannot drift apart unnoticed:
// test_shortcutsheet presses each listed key and checks the editor answers.
class ShortcutSheet : public QWidget {
    Q_OBJECT
public:
    struct Row {
        QString keys;   // as printed: "Ctrl+Z", "← →", "Shift+drag"
        QString what;
    };
    struct Group {
        QString title;
        bool videoOnly = false;
        QVector<Row> rows;
    };
    static const QVector<Group> &groups();

    // `video`: with the video and timeline groups. `maxSize`: the room in the window;
    // narrower than both columns, they stack and scroll.
    ShortcutSheet(bool video, QSize maxSize, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;   // Esc, ? and F1 close it again
};

}
