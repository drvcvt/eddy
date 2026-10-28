#include "popovers/shortcutsheet.h"
#include "app/theme.h"
#include <QBoxLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOption>

namespace eddy {

const QVector<ShortcutSheet::Group> &ShortcutSheet::groups() {
    static const QVector<Group> table{
        {QObject::tr("Tools"), false, {
            {QStringLiteral("M"), QObject::tr("Move")}, {QStringLiteral("A"), QObject::tr("Arrow")},
            {QStringLiteral("P"), QObject::tr("Pen")}, {QStringLiteral("R"), QObject::tr("Rectangle")},
            {QStringLiteral("E"), QObject::tr("Ellipse")}, {QStringLiteral("H"), QObject::tr("Highlight")},
            {QStringLiteral("T"), QObject::tr("Text")}, {QStringLiteral("N"), QObject::tr("Step")},
            {QStringLiteral("X"), QObject::tr("Redact")}, {QStringLiteral("C"), QObject::tr("Crop")}}},
        {QObject::tr("Edit"), false, {
            {QStringLiteral("Enter, Ctrl+S"), QObject::tr("Save")},
            {QStringLiteral("Ctrl+C"), QObject::tr("Copy")},
            {QStringLiteral("Ctrl+Z, Ctrl+Shift+Z"), QObject::tr("Undo, redo")},
            {QStringLiteral("Delete"), QObject::tr("Remove the selection")},
            {QStringLiteral("Ctrl+D, Alt+drag"), QObject::tr("Duplicate")},
            {QStringLiteral("Arrows, Shift"), QObject::tr("Nudge 1 px, 10 px")},
            {QStringLiteral("Shift while drawing"), QObject::tr("Keep proportions, arrows at 45°")},
            {QStringLiteral("Alt while drawing"), QObject::tr("Draw from the centre")},
            {QStringLiteral("Ctrl while moving"), QObject::tr("Place without snapping")},
            {QStringLiteral("Ctrl+Shift+S, Ctrl+O"), QObject::tr("Save, open a project")},
            {QStringLiteral("Esc"), QObject::tr("Cancel, then close")}}},
        {QObject::tr("View"), false, {
            {QStringLiteral("Wheel, + −"), QObject::tr("Zoom")},
            {QStringLiteral("0, 1"), QObject::tr("Fit, 100 %")},
            {QStringLiteral("Space+drag, middle-drag"), QObject::tr("Pan")},
            {QStringLiteral("Right-click Save"), QObject::tr("Export options")},
            {QStringLiteral("?, F1"), QObject::tr("These shortcuts")}}},
        {QObject::tr("Video and timeline"), true, {
            {QStringLiteral("Space, K"), QObject::tr("Play, pause")},
            {QStringLiteral("← →, J L"), QObject::tr("A frame back, forward")},
            {QStringLiteral("Shift+← →"), QObject::tr("A second, as it plays")},
            {QStringLiteral("↑ ↓"), QObject::tr("Previous, next edge")},
            {QStringLiteral("Home, End"), QObject::tr("Start, last frame")},
            {QStringLiteral("I, O"), QObject::tr("Start, End at the playhead")},
            {QStringLiteral("Alt+drag, Shift+I O"), QObject::tr("Mark a range")},
            {QStringLiteral("Click a piece"), QObject::tr("Choose it")},
            {QStringLiteral("Drag a piece's edge"), QObject::tr("Trim it, Alt for fine")},
            {QStringLiteral("Shift+drag a piece"), QObject::tr("Pull apart for a pause, Ctrl: no snapping")},
            {QStringLiteral("Double-click a pause, a cut"), QObject::tr("Close the gap")},
            {QStringLiteral("Delete"), QObject::tr("Take the piece or the mark out")},
            {QStringLiteral("S"), QObject::tr("Split at the playhead")},
            {QStringLiteral("Z"), QObject::tr("Add a zoom")},
            {QStringLiteral("Ctrl+Shift+C"), QObject::tr("Copy the frame")},
            {QStringLiteral("Wheel, Shift+wheel"), QObject::tr("Zoom, pan the timeline")},
            {QStringLiteral("Right-click timeline"), QObject::tr("Cut pauses, zoom, waveform")},
            {QStringLiteral("Right-click speaker"), QObject::tr("Volume, sound in the output")},
            {QStringLiteral("Right-click a cut"), QObject::tr("Restore it")}}},
    };
    return table;
}

ShortcutSheet::ShortcutSheet(bool video, QSize maxSize, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
    setObjectName(QStringLiteral("ShortcutSheet"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);   // the stylesheet's r-pop corners
    setAttribute(Qt::WA_DeleteOnClose);

    // One grid, so every description in a column starts on one edge: side by side the
    // halves fill columns 0-1 and 3-4; when that is wider than the room, all groups run
    // down 0-1 and the sheet scrolls.
    auto build = [this, video](bool stacked) {
        auto *content = new QWidget;
        content->setObjectName(QStringLiteral("ShortcutContent"));
        auto *grid = new QGridLayout(content);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(theme::kS3);
        grid->setVerticalSpacing(theme::kS1);
        grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        int rows[2] = {0, 0};
        for (int g = 0; g < groups().size(); ++g) {
            const Group &group = groups()[g];
            if (group.videoOnly && !video) continue;
            // Two even halves: for a video Tools and Edit left, the view and the clip right;
            // for an image Tools and View left, Edit right.
            const int side = stacked ? 0 : video ? int(g >= 2) : int(g == 1);
            const int col = side * 3;
            int &row = rows[side];
            if (row > 0) grid->setRowMinimumHeight(row++, theme::kS2);   // a group's gap
            auto *title = new QLabel(group.title);
            title->setObjectName(QStringLiteral("ShortcutGroup"));
            grid->addWidget(title, row++, col, 1, 2);
            for (const Row &r : group.rows) {
                auto *keys = new QLabel(r.keys);
                keys->setObjectName(QStringLiteral("ShortcutKeys"));
                auto *what = new QLabel(r.what);
                what->setObjectName(QStringLiteral("ShortcutWhat"));
                grid->addWidget(keys, row, col);
                grid->addWidget(what, row++, col + 1);
            }
        }
        // An empty column takes no spacing of its own: kS3 wide plus one kS3 spacing set the halves kS6 apart.
        if (!stacked) grid->setColumnMinimumWidth(2, theme::kS3);
        content->setParent(this);   // polished with the sheet's stylesheet before it is measured
        content->ensurePolished();
        for (auto *label : content->findChildren<QLabel *>()) label->ensurePolished();
        return content;
    };
    const int margins = 2 * theme::kS3;
    QWidget *content = build(false);
    if (content->sizeHint().width() + margins > maxSize.width()) {
        delete content;
        content = build(true);
    }

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("ShortcutScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::kS3, theme::kS3, theme::kS3, theme::kS3);
    outer->addWidget(scroll);
    const QSize wanted = content->sizeHint() + QSize(margins, margins);
    const int scrollRoom = wanted.height() > maxSize.height() ? theme::kS3 : 0;   // the grip's own lane
    setFixedSize(qMin(wanted.width() + scrollRoom, maxSize.width()), qMin(wanted.height(), maxSize.height()));
    setFocusPolicy(Qt::StrongFocus);
}

void ShortcutSheet::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Question || event->key() == Qt::Key_F1) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ShortcutSheet::paintEvent(QPaintEvent *) {
    // A translucent popup does not get its stylesheet background on its own.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

}
