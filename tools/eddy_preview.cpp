// Dev tool: render the editor chrome to a PNG for visual verification (offscreen).
// Usage: QT_QPA_PLATFORM=offscreen ./build/eddy_preview OUTPUT [dark|light] [image|native|text|arrows|picker|video|video-narrow|video-file|video-file-zoom|video-file-camera] [MEDIA]
#include "editorwindow.h"
#include "theme.h"
#include "config.h"
#include "cli.h"
#include "items/textitem.h"
#include "items/arrowitem.h"
#include "colorpopover.h"
#include "mediaio.h"
#include "videotimeline.h"
#include "cropcontroller.h"
#include "toolcontroller.h"
#include "studiodocument.h"
#include "studiostyle.h"
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
#include <QHelpEvent>
#include <QMenu>
#include <QClipboard>
#include <QElapsedTimer>
#include <memory>
#include <QPainter>

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
            QElapsedTimer loadTime; loadTime.start();
            while (player->error() == QMediaPlayer::NoError && loadTime.elapsed() < 5000
                   && (player->position() <= 0 || !output->videoSink()->videoFrame().isValid())) {
                QTimer::singleShot(50, &wait, &QEventLoop::quit);
                wait.exec();
            }
            if (player->error() != QMediaPlayer::NoError || player->position() <= 0 ||
                !output || !output->videoSink()->videoFrame().isValid()) {
                qCritical() << "Video playback failed:" << player->errorString()
                            << player->position() << player->playbackState() << player->mediaStatus()
                            << (output && output->videoSink()->videoFrame().isValid());
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
        if (mode.contains(QStringLiteral("hover"))) {
            auto *timeline = window->findChild<eddy::VideoTimeline*>();
            timeline->zoomAt(2, timeline->duration() / 2);
            timeline->hoverRequested(timeline->duration() / 2, QPoint(timeline->width() / 2, 18));
            QEventLoop previewWait;
            QTimer::singleShot(1200, &previewWait, &QEventLoop::quit);
            previewWait.exec();
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
    if (mode == QStringLiteral("tooltip")) {
        auto *fit = window->findChild<QToolButton *>(QStringLiteral("ZoomFit"));
        QHelpEvent event(QEvent::ToolTip, fit->rect().center(),
                         fit->mapToGlobal(fit->rect().center()));
        QApplication::sendEvent(fit, &event);
    }
    if (mode.contains(QStringLiteral("copyframe"))) {
        window->copyVideoFrame();
        return QApplication::clipboard()->image().save(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
    }
    if (mode.contains(QStringLiteral("crop"))) {
        window->findChild<eddy::ToolController *>()->setTool(eddy::ToolType::Crop);
        auto *crop = window->findChild<eddy::CropController *>();
        window->findChild<QMenu *>("CropRatioMenu")->actions()[2]->trigger();
        crop->press(crop->rect().topLeft(), 1, {});
        crop->move(crop->rect().topLeft() + QPointF(120, 80), {});
        crop->release();
        if (mode.contains(QStringLiteral("applied"))) crop->accept();
        app.processEvents();
    }
    QPixmap pm = window->grab();
    if (mode.contains(QStringLiteral("studio"))) {
        // Studio on with its default style; "studio-open" also paints the
        // popover where it opens (a separate popup, so grab() misses it).
        window->openStudio();
        app.processEvents();
        pm = window->grab();
        auto *popover = window->findChild<QWidget *>(QStringLiteral("StudioPopover"));
        if (mode.contains(QStringLiteral("open")) && popover) {
            QPainter painter(&pm);
            painter.drawPixmap(window->mapFromGlobal(popover->pos()), popover->grab());
        } else if (popover) {
            popover->close();
        }
    }
    if (mode.contains(QStringLiteral("zoom")) || mode.contains(QStringLiteral("camera"))) {
        // Studio on, two zooms, the first selected (context bar and mini map);
        // "camera" also paints the popover on its Camera page.
        eddy::StudioDocument doc = window->studioDocument();
        for (const auto &preset : eddy::studioBackgroundPresets())
            if (preset.kind == eddy::StudioStyle::Background::Gradient) {
                doc.style.background = preset.kind;
                doc.style.color = preset.color;
                doc.style.color2 = preset.color2;
                break;
            }
        const QRect base = window->cameraBase();
        doc.zooms = {{1, 1000, 3000, 2.0, eddy::ZoomSegment::Target::Point,
                      QPointF(base.width() * 0.7, base.height() * 0.35), eddy::ZoomSegment::Motion::Focused},
                     {2, 5000, 6500, 1.5, eddy::ZoomSegment::Target::Point,
                      QRectF(base).center(), eddy::ZoomSegment::Motion::Smooth}};
        window->setStudioDocument(doc);
        window->selectZoom(1);
        // The playback bar grows by the lane; let the layout settle first.
        QEventLoop settle;
        QTimer::singleShot(200, &settle, &QEventLoop::quit);
        settle.exec();
        pm = window->grab();
        if (mode.contains(QStringLiteral("camera"))) {
            window->openStudio();
            app.processEvents();
            auto *popover = window->findChild<QWidget *>(QStringLiteral("StudioPopover"));
            for (auto *tab : popover->findChildren<QToolButton *>(QStringLiteral("StudioPage")))
                if (tab->text() == QStringLiteral("Camera")) tab->click();
            app.processEvents();
            pm = window->grab();
            QPainter painter(&pm);
            painter.drawPixmap(window->mapFromGlobal(popover->pos()), popover->grab());
        }
    }
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/eddy-preview.png");
    pm.save(out);
    return 0;
}
