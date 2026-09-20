// Dev tool: render the editor chrome to a PNG for visual verification (offscreen).
// Usage: QT_QPA_PLATFORM=offscreen ./build/eddy_preview OUTPUT [dark|light] [image|native|text|arrows|picker|video|video-narrow|video-file] [MEDIA]
#include "editorwindow.h"
#include "theme.h"
#include "config.h"
#include "cli.h"
#include "items/textitem.h"
#include "items/arrowitem.h"
#include "colorpopover.h"
#include "mediaio.h"
#include "videotimeline.h"
#include <QApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QPixmap>
#include <QEventLoop>
#include <QTimer>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QGraphicsVideoItem>
#include <QVideoSink>
#include <QVideoFrame>
#include <QToolButton>
#include <memory>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    const bool dark = argc < 3 || QString::fromLocal8Bit(argv[2]) != QStringLiteral("light");
    app.setPalette(eddy::theme::palette(dark));
    app.setStyleSheet(eddy::theme::styleSheet(dark));

    eddy::Config cfg; eddy::CliOptions cli;
    cfg.strokeColor = QColor(eddy::theme::kStroke);
    cfg.animations = false;                  // static frame for a clean grab
    std::unique_ptr<eddy::EditorWindow> window;
    const QString mode = argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QString();
    if (mode == QStringLiteral("picker")) {
        eddy::ColorPopover picker(nullptr, QColor("#ff3b30"));
        picker.show();
        app.processEvents();
        return picker.grab().save(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
    }
    if (mode.startsWith(QStringLiteral("video"))) {
        eddy::MediaDocument media;
        media.kind = eddy::MediaKind::Video;
        media.path = QStringLiteral("/tmp/eddy-preview-video.mp4");
        media.video.size = QSize(900, 560);
        media.video.durationMs = 187000;
        media.video.fps = 60.0;
        if (mode.startsWith(QStringLiteral("video-file")) && argc >= 5) {
            const auto loaded = eddy::loadMediaInput({eddy::InputSpec::File, QString::fromLocal8Bit(argv[4])});
            if (!loaded.ok) { qCritical() << loaded.error; return 1; }
            media = loaded.document;
        }
        window = std::make_unique<eddy::EditorWindow>(media, cfg, cli);
    } else {
        QImage bg(900, 560, QImage::Format_ARGB32_Premultiplied);
        bg.fill(QColor("#202020"));
        if (argc >= 5) {
            QImage source(QString::fromLocal8Bit(argv[4]));
            if (source.isNull()) return 1;
            bg = source;
        }
        eddy::MediaDocument media;
        media.kind = eddy::MediaKind::Image;
        media.image = bg;
        media.path = argc >= 5 ? QString::fromLocal8Bit(argv[4]) : QString();
        window = std::make_unique<eddy::EditorWindow>(media, cfg, cli);
    }
    if (mode != QStringLiteral("native"))
        window->resize(mode.contains(QStringLiteral("narrow")) ? QSize(520, 620) : QSize(1000, 680));
    window->show();
    app.processEvents();
    if (mode.startsWith(QStringLiteral("video"))) {
        if (mode.startsWith(QStringLiteral("video-file")))
            window->findChild<QToolButton*>("PlaybackPlay")->click();
        auto *player = window->findChild<QMediaPlayer*>();
        if (mode.startsWith(QStringLiteral("video-file"))) {
            if (!player) return 1;
            player->audioOutput()->setMuted(true);
            player->play();
        }
        QEventLoop wait;
        QTimer::singleShot(mode.startsWith(QStringLiteral("video-file")) ? 1500 : 400, &wait, &QEventLoop::quit);
        wait.exec();
        app.processEvents();
        if (mode.startsWith(QStringLiteral("video-file"))) {
            auto *output = qobject_cast<QGraphicsVideoItem*>(player->videoOutput());
            if (player->error() != QMediaPlayer::NoError || player->position() <= 0 ||
                !output || !output->videoSink()->videoFrame().isValid()) {
                qCritical() << "Video playback failed:" << player->errorString();
                return 1;
            }
            qInfo() << "Decoded video frame; playback at" << player->position() << "ms";
            player->pause();
        }
        if (mode.contains(QStringLiteral("trim"))) {
            auto *timeline = window->findChild<eddy::VideoTimeline*>();
            timeline->setTrimRange(timeline->duration() / 5, timeline->duration() * 4 / 5);
            timeline->trimPreviewed(timeline->trimIn(), timeline->trimOut());
        }
    }
    if (mode == QStringLiteral("arrows")) {
        auto *scene = window->findChild<QGraphicsScene*>();
        for (int i = 0; i < 4; ++i) {
            auto *arrow = new eddy::ArrowItem(QPointF(100, 100 + 100 * i), QPointF(740, 55 + 105 * i));
            arrow->setStrokeWidth(2 + 2 * i);
            arrow->setStrokeColor(QColor("#ff3b30"));
            scene->addItem(arrow);
        }
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
