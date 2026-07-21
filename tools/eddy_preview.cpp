// Dev tool: render the editor chrome to a PNG for visual verification (offscreen).
// Usage: QT_QPA_PLATFORM=offscreen ./build/eddy_preview /tmp/eddy-preview.png [dark|light]
#include "editorwindow.h"
#include "theme.h"
#include "config.h"
#include "cli.h"
#include "items/textitem.h"
#include <QApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QPixmap>
#include <QEventLoop>
#include <QTimer>
#include <memory>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    const bool dark = argc < 3 || QString::fromLocal8Bit(argv[2]) != QStringLiteral("light");
    app.setPalette(eddy::theme::palette(dark));
    app.setStyleSheet(eddy::theme::styleSheet(dark));

    eddy::Config cfg; eddy::CliOptions cli;
    cfg.animations = false;                  // static frame for a clean grab
    std::unique_ptr<eddy::EditorWindow> window;
    const QString mode = argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QString();
    if (mode.startsWith(QStringLiteral("video"))) {
        eddy::MediaDocument media;
        media.kind = eddy::MediaKind::Video;
        media.path = QStringLiteral("/tmp/eddy-preview-video.mp4");
        media.video.size = QSize(900, 560);
        media.video.durationMs = 187000;
        media.video.fps = 60.0;
        window = std::make_unique<eddy::EditorWindow>(media, cfg, cli);
    } else {
        QImage bg(900, 560, QImage::Format_ARGB32_Premultiplied);
        bg.fill(QColor("#243042"));
        window = std::make_unique<eddy::EditorWindow>(bg, cfg, cli);
    }
    window->resize(mode == QStringLiteral("video-narrow") ? QSize(520, 620) : QSize(900, 620));
    window->show();
    app.processEvents();
    if (mode.startsWith(QStringLiteral("video"))) {
        QEventLoop wait;
        QTimer::singleShot(400, &wait, &QEventLoop::quit);
        wait.exec();
        app.processEvents();
    }
    if (mode == QStringLiteral("text")) {
        auto *text = new eddy::TextItem(QStringLiteral("Move and style me"), QColor("#ececec"), 20);
        text->setPos(330, 250);
        text->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        window->findChild<QGraphicsScene *>()->addItem(text);
        text->setSelected(true);
        app.processEvents();
    }
    QPixmap pm = window->grab();
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/eddy-preview.png");
    pm.save(out);
    return 0;
}
