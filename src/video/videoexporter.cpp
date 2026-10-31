#include "video/videoexporter.h"
#include <QLineF>
#include "output/exportsettings.h"
#include <algorithm>
#include "output/exporter.h"
#include "video/mediaio.h"
#include "studio/camerapath.h"
#include "output/renderexport.h"
#include "studio/studiorenderer.h"
#include "video/timemap.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <unistd.h>
#include <filesystem>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace eddy {

static QStringList deviceArgs(const QString &encoder) {
    const QString type = encoder.endsWith("_vulkan") ? QStringLiteral("vulkan")
        : encoder.endsWith("_vaapi") ? QStringLiteral("vaapi") : QString();
    return type.isEmpty() ? QStringList() : QStringList{
        "-init_hw_device", type + "=eddy", "-filter_hw_device", "eddy"};
}

static QString hardwareEncoder(const QString &ffmpeg) {
    // Probe an actual frame once per executable, off the GUI thread. An encoder
    // appearing in ffmpeg's list alone does not mean its device/driver works.
    static QMutex mutex;
    static QHash<QString, QString> cache;
    QMutexLocker lock(&mutex);
    if (cache.contains(ffmpeg)) return cache.value(ffmpeg);
    for (const QString &encoder : {QStringLiteral("h264_nvenc"),
                                   QStringLiteral("h264_vulkan"), QStringLiteral("h264_vaapi")}) {
        const QStringList device = deviceArgs(encoder);
        QProcess probe;
        probe.start(ffmpeg, QStringList{"-v", "error", "-nostdin"} + device + QStringList{
            "-f", "lavfi", "-i", "color=size=320x180:rate=30",
            "-vf", device.isEmpty() ? "format=nv12" : "format=nv12,hwupload",
            "-frames:v", "1", "-c:v", encoder, "-bf", "0", "-f", "null", "-"});
        if (!probe.waitForFinished(1000)) { probe.kill(); probe.waitForFinished(1000); }
        else if (probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            cache.insert(ffmpeg, encoder);
            return encoder;
        }
    }
    cache.insert(ffmpeg, {});
    return {};
}

// Whether a CUDA device opens, once per executable, off the GUI thread.
static bool cudaDecoder(const QString &ffmpeg) {
    static QMutex mutex;
    static QHash<QString, bool> cache;
    QMutexLocker lock(&mutex);
    if (const auto known = cache.constFind(ffmpeg); known != cache.cend()) return *known;
    QProcess probe;
    probe.start(ffmpeg, QStringList{"-v", "error", "-nostdin", "-init_hw_device", "cuda=eddy",
                                    "-f", "lavfi", "-i", "color=size=64x64", "-frames:v", "1", "-f", "null", "-"});
    bool works = false;
    if (!probe.waitForFinished(2000)) { probe.kill(); probe.waitForFinished(1000); }
    else works = probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    cache.insert(ffmpeg, works);
    return works;
}

// The source's input options and the start of its filter chain. On the GPU, surplus frames
// go before the download (a 240 fps recording costs a quarter), and nv12 turns back into
// yuv420p first: so the frames match ffmpeg's own decoder bit for bit (export-gpu plan).
static QStringList decodeOptions(bool gpu) {
    return gpu ? QStringList{"-hwaccel", "cuda", "-hwaccel_output_format", "cuda"} : QStringList();
}

static QString decodeHead(bool gpu, int fps) {
    return QStringLiteral("[0:v]") + (fps > 0 ? QStringLiteral("fps=%1,").arg(fps) : QString())
        + (gpu ? QStringLiteral("hwdownload,format=nv12,format=yuv420p,") : QString());
}

// Where `image` has ink, grown to even edges, empty when it has none. ffmpeg blends an
// overlay over its whole size, so only this part goes in; on even edges the half-size
// colour planes blend the same, the output is bit for bit as with the whole image (plan E5).
static QRect inkRect(const QImage &image) {
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int left = argb.width(), top = argb.height(), right = -1, bottom = -1;
    for (int y = 0; y < argb.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x)
            if (qAlpha(row[x])) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    }
    if (right < 0) return {};
    const int x = left & ~1, y = top & ~1;
    return QRect(x, y, std::min(argb.width(), (right + 2) & ~1) - x, std::min(argb.height(), (bottom + 2) & ~1) - y);
}

// Blurred patches over `current`, each a boxblurred crop overlaid in place.
// Returns the label of the result.
// Timed ones show only in their stretch, with `t` counted from the input's
// first frame at `originMs`.
static QString appendBlurFilters(QString &filter, QString current, const QVector<QRect> &rects, QSize bounds,
                                 const QVector<VideoExportRequest::TimedBlur> &timed = {}, double originMs = 0) {
    constexpr int videoBlurRadius = 12;
    int blurIndex = 0;
    QVector<QPair<QRect, QString>> all;
    for (const QRect &rect : rects) all.append({rect, QString()});
    for (const auto &blur : timed)
        all.append({blur.rect, QStringLiteral(":enable='between(t\\,%1\\,%2)'")
                                   .arg((blur.fromMs - originMs) / 1000.0, 0, 'f', 3)
                                   .arg((blur.toMs - originMs) / 1000.0, 0, 'f', 3)});
    for (const auto &[requested, enable] : all) {
        const QRect rect = requested.intersected(QRect(QPoint(), bounds));
        if (rect.isEmpty()) continue;
        const QString base = QStringLiteral("[blurbase%1]").arg(blurIndex);
        const QString crop = QStringLiteral("[blurcrop%1]").arg(blurIndex);
        const QString blurred = QStringLiteral("[blurpatch%1]").arg(blurIndex);
        const QString next = QStringLiteral("[blurvideo%1]").arg(blurIndex);
        filter += current + QStringLiteral("split=2") + base + crop + QStringLiteral(";");
        filter += crop + QStringLiteral(
            "crop=%1:%2:%3:%4,boxblur="
            "luma_radius=min(%5\\,(min(w\\,h)-1)/2):luma_power=2:"
            "chroma_radius=min(%5\\,(min(cw\\,ch)-1)/2):chroma_power=2")
            .arg(rect.width()).arg(rect.height()).arg(rect.x()).arg(rect.y())
            .arg(videoBlurRadius) + blurred + QStringLiteral(";");
        filter += base + blurred + QStringLiteral("overlay=%1:%2:format=auto%3")
            .arg(rect.x()).arg(rect.y()).arg(enable) + next + QStringLiteral(";");
        current = next;
        ++blurIndex;
    }
    return current;
}

static QString seconds(double ms) { return QString::number(ms / 1000.0, 'f', 4); }

// The kept pieces of the input after `label`, one after another: each trimmed
// relative to the input's first frame at `originMs` and sped up. The result is
// the unlabelled end of the chain (studio plan 4.3).
// P: a pause holds the frame before it (tpad clone) or shows black, in output time after
// setpts; one opening the output holds the first frame. Measured: docs/plans/2026-10/2026-10-28-pauses.md.
static QString pausePad(const TimePiece &after, bool atStart) {
    if (after.pauseBefore <= 0) return {};
    const QString side = atStart ? QStringLiteral("start") : QStringLiteral("stop");
    return QStringLiteral(",tpad=%1_mode=%2:%1_duration=%3%4")
        .arg(side, after.pauseBlack ? QStringLiteral("add") : QStringLiteral("clone"), seconds(after.pauseBefore),
             after.pauseBlack ? QStringLiteral(":color=black") : QString());
}

static QString fragmentVideo(const QVector<TimePiece> &pieces, double originMs, const QString &label) {
    QString f = label + QStringLiteral("split=%1").arg(pieces.size());
    for (int i = 0; i < pieces.size(); ++i) f += QStringLiteral("[fs%1]").arg(i);
    f += QLatin1Char(';');
    for (int i = 0; i < pieces.size(); ++i)
        f += QStringLiteral("[fs%1]trim=start=%2:end=%3,setpts=(PTS-STARTPTS)/%4%5%6[fv%1];")
                 .arg(i).arg(seconds(pieces[i].srcStart - originMs), seconds(pieces[i].srcEnd - originMs))
                 .arg(pieces[i].speed)
                 .arg(i == 0 ? pausePad(pieces[0], true) : QString(),
                      i + 1 < pieces.size() ? pausePad(pieces[i + 1], false) : QString());
    for (int i = 0; i < pieces.size(); ++i) f += QStringLiteral("[fv%1]").arg(i);
    return f + QStringLiteral("concat=n=%1:v=1:a=0").arg(pieces.size());
}

// The same pieces of `input`'s sound, ending in [aout]. atempo takes 0.5 to 2
// per stage, so slower and faster speeds chain it.
static QString fragmentAudio(const QVector<TimePiece> &pieces, double originMs, const QString &input) {
    QString f = input + QStringLiteral("asplit=%1").arg(pieces.size());
    for (int i = 0; i < pieces.size(); ++i) f += QStringLiteral("[as%1]").arg(i);
    f += QLatin1Char(';');
    for (int i = 0; i < pieces.size(); ++i) {
        QString tempo;
        double speed = pieces[i].speed;
        for (; speed < 0.5; speed /= 0.5) tempo += QStringLiteral(",atempo=0.5");
        for (; speed > 2.0; speed /= 2.0) tempo += QStringLiteral(",atempo=2");
        if (speed != 1.0) tempo += QStringLiteral(",atempo=%1").arg(speed);
        // P: silence for a pause, after this piece or (opening the output) before it.
        QString silence;
        if (i == 0 && pieces[0].pauseBefore > 0)
            silence += QStringLiteral(",adelay=delays=%1:all=1").arg(qRound64(pieces[0].pauseBefore));
        if (i + 1 < pieces.size() && pieces[i + 1].pauseBefore > 0)
            silence += QStringLiteral(",apad=pad_dur=%1").arg(seconds(pieces[i + 1].pauseBefore));
        f += QStringLiteral("[as%1]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS%4%5[fa%1];")
                 .arg(i).arg(seconds(pieces[i].srcStart - originMs), seconds(pieces[i].srcEnd - originMs), tempo, silence);
    }
    for (int i = 0; i < pieces.size(); ++i) f += QStringLiteral("[fa%1]").arg(i);
    return f + QStringLiteral("concat=n=%1:v=0:a=1[aout]").arg(pieces.size());
}

// Codec arguments for `encoder`; empty means the CPU arguments as given.
static QStringList encoderCodecArgs(const QString &encoder, const QStringList &cpuArgs, bool trimmed) {
    if (encoder.isEmpty()) return cpuArgs;
    QStringList codecs = {"-c:v", encoder, "-bf", "0",
        encoder.endsWith("_vaapi") ? "-global_quality" : "-qp", "18",
        "-c:a", trimmed ? "aac" : "copy", "-movflags", "+faststart"};
    if (deviceArgs(encoder).isEmpty()) codecs += {"-pix_fmt", "yuv420p"};
    if (trimmed) codecs += {"-b:a", "192k"};
    return codecs;
}

// The rename that puts an output written next to its own input in place.
static DeliverResult finishOutput(bool replaceInput, const QString &written, const QString &output) {
    if (replaceInput) {
        auto renamed = replaceFileAtomically(written, output);
        if (!renamed.ok) {
            QFile::remove(written);
            return renamed;
        }
    }
    DeliverResult r;
    r.ok = true;
    return r;
}

static QString sameDirTempTemplate(const QString &outputPath) {
    const QFileInfo out(outputPath);
    const QString suffix = out.suffix().isEmpty() ? QStringLiteral("mp4") : out.suffix();
    return out.absoluteDir().filePath(QStringLiteral(".eddy-video-XXXXXX.") + suffix);
}

static bool sameExistingPath(const QString &a, const QString &b) {
    const QString ca = QFileInfo(a).canonicalFilePath();
    const QString cb = QFileInfo(b).canonicalFilePath();
    return !ca.isEmpty() && ca == cb;
}

DeliverResult replaceFileAtomically(const QString &from, const QString &to) {
    DeliverResult r;
#ifdef Q_OS_WIN
    const std::wstring fromPath = from.toStdWString();
    const std::wstring toPath = to.toStdWString();
    if (!::ReplaceFileW(toPath.c_str(), fromPath.c_str(), nullptr,
                        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
        // ReplaceFileW requires an existing destination; fall back to a plain
        // move so this behaves like the POSIX rename when the target is new.
        const DWORD replaceError = ::GetLastError();
        const bool destinationMissing = replaceError == ERROR_FILE_NOT_FOUND
            || replaceError == ERROR_PATH_NOT_FOUND;
        if (!destinationMissing
            || !::MoveFileExW(fromPath.c_str(), toPath.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const std::error_code ec(int(replaceError), std::system_category());
            r.error = QStringLiteral("cannot replace ") + to + QStringLiteral(": ")
                    + QString::fromStdString(ec.message());
            return r;
        }
    }
#else
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from.toStdString()),
                            std::filesystem::path(to.toStdString()), ec);
    if (ec) {
        r.error = QStringLiteral("cannot replace ") + to + QStringLiteral(": ")
                + QString::fromStdString(ec.message());
        return r;
    }
#endif
    r.ok = true;
    return r;
}

// The frame-rendered export: ffmpeg decodes (orientation, sample aspect, blur,
// 60 fps), Qt draws the camera's view and the Studio frame, ffmpeg encodes and
// takes the audio straight from the source (studio plan 4.2).
static DeliverResult writeRendered(const VideoExportRequest &req, const QString &ffmpeg,
                                   const QString &output, const QStringList &cpuCodecs,
                                   const VideoInfo &source, bool overlayVisible,
                                   const QElapsedTimer &elapsed) {
    const bool trimmed = req.trimOutMs >= 0;
    const qint64 endMs = trimmed ? req.trimOutMs : source.durationMs;
    const QRect content = req.cropRect.isNull() ? req.overlay.rect() : req.cropRect;
    const QRect view = req.baseView.isNull() ? content : req.baseView;
    const TimeMap time(source.durationMs, req.trimInMs, endMs, req.fragments);
    const bool pieces = !req.fragments.isEmpty();
    const bool sound = pieces && source.hasAudio && req.includeAudio;
    const CameraPath camera(req.zooms, time,
                            CameraFrame{QRectF(content),
                                        view == content ? 0.0 : double(view.width()) / view.height(),
                                        QRectF(view).center(), req.cursorTrack.get(), req.baseFollowsCursor});
    StudioRenderer renderer(req.overlay.size(), view, req.studio,
                            overlayVisible ? req.overlay : QImage());
    renderer.setTimedOverlays(req.timedOverlays);
    const QSize size = renderer.outputSize();
    const int fps = std::clamp(req.maxFps, 1, 60);
    const QSize finalSize = exportSize(size, req.maxShortSide);
    const bool gif = QFileInfo(req.outputPath).suffix().compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0;
    QStringList seek;
    if (req.trimInMs > 0) seek = {"-ss", QString::number(req.trimInMs / 1000.0, 'f', 3)};
    QStringList length;
    if (trimmed) length = {"-t", QString::number((req.trimOutMs - req.trimInMs) / 1000.0, 'f', 3)};

    QString filter = QStringLiteral("scale=%1:%2,setsar=1[base];")
        .arg(req.overlay.width()).arg(req.overlay.height());
    const QString blurred = appendBlurFilters(filter, QStringLiteral("[base]"), req.blurRects,
                                              req.overlay.size(), req.timedBlurs, req.trimInMs);
    if (pieces)   // sped-up pieces change the rate, so fps comes again after them
        filter += blurred + QStringLiteral("null[pre];") + fragmentVideo(time.pieces(), req.trimInMs, QStringLiteral("[pre]"))
                  + QStringLiteral(",fps=%1,format=bgra[v]").arg(fps);
    else
        filter += blurred + QStringLiteral("format=bgra[v]");
    RenderPipeline job;
    job.ffmpeg = ffmpeg;
    job.sourceSize = req.overlay.size();
    // With pieces the length limits what is read, not what comes out.
    auto decodeArgs = [&](bool gpu) {
        return QStringList{"-hide_banner", "-loglevel", "error", "-nostdin"} + seek
            + (pieces ? length : QStringList()) + decodeOptions(gpu) + QStringList{"-i", req.inputPath}
            + (pieces ? QStringList() : length)
            + QStringList{"-filter_complex", decodeHead(gpu, fps) + filter, "-map", "[v]", "-f", "rawvideo", "-pix_fmt", "bgra", "-"};
    };
    job.outputSize = size;
    job.expectedFrames = qRound64(time.outputDurationMs() * fps / 1000.0);
    // Motion blur: six moments within a shutter of up to half a frame, only
    // while the camera moves visibly; a still camera renders once.
    const double shutter = std::clamp(req.motionBlur, 0, 100) / 100.0 * 500.0 / fps;
    job.render = [&](qint64 index, const QImage &frame, QImage &out) {
        const double outMs = index * 1000.0 / fps;
        const double sourceMs = time.toSource(outMs);
        if (shutter > 0) {
            const QRectF a = camera.rectAt(outMs - shutter / 2), b = camera.rectAt(outMs + shutter / 2);
            const double moved = std::max({QLineF(a.topLeft(), b.topLeft()).length(),
                                           QLineF(a.bottomRight(), b.bottomRight()).length()})
                                 * size.width() / std::max(1.0, a.width());
            if (moved > 0.5 && !camera.cutWithin(outMs - shutter / 2, outMs + shutter / 2)) {
                QVector<QRectF> cameras;
                for (int i = 0; i < 6; ++i) cameras.append(camera.rectAt(outMs - shutter / 2 + shutter * i / 5));
                renderer.renderBlurred(frame, cameras, out, sourceMs);
                return;
            }
        }
        renderer.render(frame, camera.rectAt(outMs), out, sourceMs);
    };
    job.stallTimeoutMs = req.stallTimeoutMs;
    job.progress = req.progress;
    job.cancelled = req.cancelled;

    const QString ext = QFileInfo(req.outputPath).suffix().toLower();
    const QString hardware = ext != "webm" && !gif && req.capKbps <= 0 && (req.timeoutMs < 0 || req.timeoutMs >= 5000)
        ? hardwareEncoder(ffmpeg) : QString();
    QStringList encoders;
    if (!hardware.isEmpty()) encoders.append(hardware);
    encoders.append(QString());   // the CPU encoder also takes what the hardware one refuses
    const QString shrink = finalSize == size ? QString()
        : QStringLiteral("scale=%1:%2:flags=lanczos,").arg(finalSize.width()).arg(finalSize.height());
    DeliverResult r;
    bool gpu = source.gpuDecodable && cudaDecoder(ffmpeg);
    for (int e = 0; e < encoders.size(); ++e) {
        const QString &encoder = encoders[e];
        const QStringList device = deviceArgs(encoder);
        // eddy turns its frames into BT.709 yuv420p, tagged so: a third of the bytes of bgra
        // through the pipe, the zoom export's floor (plan E7). GIF keeps full colour for its palette.
        job.yuvOutput = !gif;
        const QStringList input = QStringList{"-hide_banner", "-loglevel", "error", "-y"} + device
            + QStringList{"-f", "rawvideo", "-pix_fmt", gif ? "bgra" : "yuv420p", "-s",
                          QStringLiteral("%1x%2").arg(size.width()).arg(size.height()), "-r", QString::number(fps)}
            + (gif ? QStringList() : QStringList{"-colorspace", "bt709", "-color_range", "tv"}) + QStringList{"-i", "-"};
        if (gif) {
            // One palette for the file; GIF has no sound.
            job.encodeArgs = input + QStringList{"-map", "0:v", "-vf", shrink + QStringLiteral(
                "split[ga][gb];[ga]palettegen=stats_mode=diff[gp];"
                "[gb][gp]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle"), "-an", output};
        } else {
            // The BT.709 tags of the input go on to the output, so players do not guess.
            const QStringList audio = !req.includeAudio ? QStringList()
                : !pieces ? QStringList{"-map", "1:a?"}
                : sound ? QStringList{"-filter_complex", fragmentAudio(time.pieces(), req.trimInMs, QStringLiteral("[1:a]")),
                                      "-map", "[aout]"}
                        : QStringList();
            job.encodeArgs = input + seek + length + QStringList{"-i", req.inputPath, "-map", "0:v"} + audio
                + QStringList{"-vf", shrink + (device.isEmpty() ? QStringLiteral("format=yuv420p") : QStringLiteral("format=nv12,hwupload"))}
                + encoderCodecArgs(encoder, cpuCodecs, trimmed || pieces) + QStringList{"-shortest", output};
        }
        job.decodeArgs = decodeArgs(gpu);
        job.timeoutMs = req.timeoutMs < 0 ? -1 : qMax(0, req.timeoutMs - int(elapsed.elapsed()));
        r = runRenderPipeline(job);
        if (r.ok || r.error.contains(QStringLiteral("cancelled")) || r.error.contains(QStringLiteral("timed out")))
            return r;
        if (gpu) { gpu = false; --e; continue; }   // the same encoder again, decoding on the CPU
        if (!encoder.isEmpty()) r.error += QStringLiteral(" with ") + encoder;
    }
    return r;
}

static DeliverResult writeVideo(const VideoExportRequest &req, bool render) {
    QElapsedTimer elapsed;
    elapsed.start();
    DeliverResult r;
    if (req.outputPath == QStringLiteral("-")) {
        r.error = QStringLiteral("video export to stdout is not supported");
        return r;
    }
    if (req.inputPath.isEmpty() || req.outputPath.isEmpty()) {
        r.error = QStringLiteral("video export needs input and output paths");
        return r;
    }
    if (req.overlay.isNull()) {
        r.error = QStringLiteral("video export needs a non-null overlay image");
        return r;
    }
    const QRect crop = req.cropRect;
    if (crop != QRect() && (crop.isEmpty() || !req.overlay.rect().contains(crop)
        || crop.x() % 2 || crop.y() % 2 || crop.width() % 2 || crop.height() % 2)) {
        r.error = QStringLiteral("video crop must fit the source and use even pixel coordinates");
        return r;
    }
    const QRect bounds = crop.isNull() ? req.overlay.rect() : crop;
    const QRect view = req.baseView;
    if (view != QRect() && (view.isEmpty() || !bounds.contains(view)
        || view.x() % 2 || view.y() % 2 || view.width() % 2 || view.height() % 2)) {
        r.error = QStringLiteral("video base view must fit the crop and use even pixel coordinates");
        return r;
    }
    // What the frame holds while the camera rests: the base view, else the crop.
    const QRect framed = view.isNull() ? crop : view;
    const bool trimmed = req.trimOutMs >= 0;
    const bool pieces = !req.fragments.isEmpty();
    const bool reencodeAudio = trimmed || pieces;
    if (req.trimInMs < 0 || (trimmed && req.trimOutMs <= req.trimInMs)) {
        r.error = QStringLiteral("invalid video trim range");
        return r;
    }

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        r.error = QStringLiteral("ffmpeg not found");
        return r;
    }

    const QRect overlayInk = inkRect(req.overlay);
    const bool overlayVisible = !overlayInk.isEmpty();
    QTemporaryFile overlayTmp(QDir::tempPath() + QStringLiteral("/eddy-overlay-XXXXXX.png"));
    if (overlayVisible && !render) {
        if (!overlayTmp.open()) {
            r.error = QStringLiteral("cannot create temporary overlay");
            return r;
        }
        const QByteArray png = encodePng(req.overlay.copy(overlayInk));
        if (png.isEmpty() || overlayTmp.write(png) != png.size()) {
            r.error = QStringLiteral("cannot write temporary overlay");
            return r;
        }
        overlayTmp.close();
    }
    const QString overlayPath = overlayTmp.fileName();
    // Timed overlays are stills too, one input each.
    std::vector<std::unique_ptr<QTemporaryFile>> timedFiles;
    QVector<QPoint> timedAt;
    if (!render) {
        for (const TimedOverlay &timed : req.timedOverlays) {
            auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/eddy-timed-XXXXXX.png"));
            const QRect ink = inkRect(timed.image);
            const QRect part = ink.isEmpty() ? QRect(0, 0, std::min(2, timed.image.width()), std::min(2, timed.image.height())) : ink;
            timedAt.append(part.topLeft());
            const QByteArray png = encodePng(timed.image.copy(part));
            if (!file->open() || png.isEmpty() || file->write(png) != png.size()) {
                r.error = QStringLiteral("cannot write a temporary overlay");
                return r;
            }
            file->close();
            timedFiles.push_back(std::move(file));
        }
    }

    // Studio framing: an opaque background still and a coverage mask, merged
    // with the video padded to the output size.
    const QSize contentSize = framed.isNull() ? req.overlay.size() : framed.size();
    const StudioLayout studio = studioLayout(contentSize, req.studio);
    QTemporaryFile studioBackground(QDir::tempPath() + QStringLiteral("/eddy-studio-bg-XXXXXX.png"));
    QTemporaryFile studioMask(QDir::tempPath() + QStringLiteral("/eddy-studio-mask-XXXXXX.png"));
    if (req.studio.active() && !render) {
        const std::pair<QTemporaryFile *, QImage> stills[] = {
            {&studioBackground, renderStudioBackground(contentSize, req.studio)},
            {&studioMask, renderStudioFrameMask(contentSize, req.studio)}};
        for (const auto &[file, image] : stills) {
            const QByteArray png = encodePng(image);
            if (!file->open() || png.isEmpty() || file->write(png) != png.size()) {
                r.error = QStringLiteral("cannot write temporary studio frame");
                return r;
            }
            file->close();
        }
    }

    QString actualOutput = req.outputPath;
    QTemporaryFile samePathOutput;
    const bool replaceInput = sameExistingPath(req.inputPath, req.outputPath);
    if (replaceInput) {
        samePathOutput.setFileTemplate(sameDirTempTemplate(req.outputPath));
        if (!samePathOutput.open()) {
            QFile::remove(overlayPath);
            r.error = QStringLiteral("cannot create temporary output next to ") + req.outputPath;
            return r;
        }
        actualOutput = samePathOutput.fileName();
        samePathOutput.close();
    }

    const QString ext = QFileInfo(req.outputPath).suffix().toLower();
    const bool gif = ext == QStringLiteral("gif");
    QStringList codecArgs;
    if (gif) {
        codecArgs = {QStringLiteral("-an")};
    } else if (ext == QStringLiteral("webm")) {
        codecArgs = {
            QStringLiteral("-c:v"), QStringLiteral("libvpx-vp9"),
            QStringLiteral("-deadline"), QStringLiteral("good"),
            QStringLiteral("-cpu-used"), QStringLiteral("4"),
            QStringLiteral("-crf"), QStringLiteral("18"),
            QStringLiteral("-b:v"), QStringLiteral("0"),
            QStringLiteral("-row-mt"), QStringLiteral("1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-c:a"), QStringLiteral("libopus"),
        };
    } else {
        codecArgs = {
            QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-preset"), QStringLiteral("veryfast"),
            QStringLiteral("-crf"), QStringLiteral("18"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-c:a"), reencodeAudio ? QStringLiteral("aac") : QStringLiteral("copy"),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        };
        if (reencodeAudio)
            codecArgs += {QStringLiteral("-b:a"), QStringLiteral("192k")};
    }
    if (req.capKbps > 0 && !gif) {
        // Under a size limit: quality as usual, the rate capped where it would not fit,
        // and a smaller sound track (a copied one could be any size).
        const QString rate = QString::number(req.capKbps) + QLatin1Char('k');
        const int bitrate = codecArgs.indexOf(QStringLiteral("-b:v"));
        if (bitrate >= 0) codecArgs[bitrate + 1] = rate;   // VP9: constrained quality
        else codecArgs += {QStringLiteral("-maxrate"), rate, QStringLiteral("-bufsize"), rate};
        const int audio = codecArgs.indexOf(QStringLiteral("-c:a"));
        if (audio >= 0 && codecArgs[audio + 1] == QLatin1String("copy")) codecArgs[audio + 1] = QStringLiteral("aac");
        const int audioRate = codecArgs.indexOf(QStringLiteral("-b:a"));
        const QString small = ext == QStringLiteral("webm") ? QStringLiteral("64k") : QStringLiteral("96k");
        if (audioRate >= 0) codecArgs[audioRate + 1] = small;
        else codecArgs += {QStringLiteral("-b:a"), small};
    }

    // ffmpeg autorotates before the filter graph. Normalize sample aspect ratio
    // to the same display-pixel coordinate system used by the editor.
    // Drop surplus frames before the filters: -fpsmax alone still scales,
    // blurs and overlays every frame of a 240 fps recording.
    const ProbeVideoResult probed = probeVideoFile(req.inputPath);
    const VideoInfo source = probed.info;
    // Pieces need to know the length and whether there is sound to cut along.
    if (pieces && !probed.ok) {
        r.error = probed.error;
        return r;
    }
    const int maxFps = std::clamp(req.maxFps, 1, 60);
    const bool highFps = source.fps > maxFps;
    const TimeMap time(source.durationMs, req.trimInMs, trimmed ? req.trimOutMs : source.durationMs, req.fragments);
    if (pieces && time.pieces().isEmpty()) {
        r.error = QStringLiteral("every part of the trimmed range is cut");
        return r;
    }
    const qint64 outputMs = qRound64(time.outputDurationMs());
    if (render) {
        const DeliverResult rendered = writeRendered(req, ffmpeg, actualOutput, codecArgs, source,
                                                     overlayVisible, elapsed);
        return rendered.ok ? finishOutput(replaceInput, actualOutput, req.outputPath) : rendered;
    }
    QString filter = QStringLiteral("scale=%1:%2,setsar=1[base];")   // after decodeHead
        .arg(req.overlay.width()).arg(req.overlay.height());
    const QString current = appendBlurFilters(filter, QStringLiteral("[base]"), req.blurRects,
                                              req.overlay.size(), req.timedBlurs, req.trimInMs);
    filter += current + (overlayVisible ? QStringLiteral("[1:v]overlay=%1:%2:format=auto").arg(overlayInk.x()).arg(overlayInk.y())
                                       : QStringLiteral("null"));
    // Timed overlays follow the static one, each only inside its stretch.
    const int firstTimed = overlayVisible ? 2 : 1;
    for (int i = 0; i < timedFiles.size(); ++i)
        filter += QStringLiteral("[tov%1];[tov%1][%2:v]overlay=%5:%6:format=auto:enable='between(t\\,%3\\,%4)'")
                      .arg(i).arg(firstTimed + i)
                      .arg((req.timedOverlays[i].fromMs - req.trimInMs) / 1000.0, 0, 'f', 3)
                      .arg((req.timedOverlays[i].toMs - req.trimInMs) / 1000.0, 0, 'f', 3)
                      .arg(timedAt[i].x()).arg(timedAt[i].y());
    // Pieces come after blur and annotations, which live in source time, and
    // before crop and framing (studio plan 4.3).
    if (pieces)
        filter += QStringLiteral("[pre];") + fragmentVideo(time.pieces(), req.trimInMs, QStringLiteral("[pre]"));
    const bool sound = pieces && !gif && source.hasAudio && req.includeAudio;
    const QString audioFilter = sound ? QStringLiteral(";") + fragmentAudio(time.pieces(), req.trimInMs, QStringLiteral("[0:a]"))
                                      : QString();
    if (!framed.isNull())
        filter += QStringLiteral(",crop=%1:%2:%3:%4").arg(framed.width()).arg(framed.height())
                      .arg(framed.x()).arg(framed.y());
    if (req.studio.active()) {
        // maskedmerge on planar yuv is SIMD work, where an RGBA overlay of the
        // whole output more than doubled export time. The stills are single
        // frames that framesync repeats, so the video keeps the frame timing.
        // Chroma planes take the mask at half size. Qt's PNGs carry a DPI that
        // ffmpeg reads as a 3780:3780 aspect, which mergeplanes rejects.
        const int background = firstTimed + int(timedFiles.size());
        filter += QStringLiteral(",pad=%1:%2:%3:%4,format=yuv420p[padded];"
                                 "[%5:v]setsar=1,format=yuv420p[studiobg];"
                                 "[%6:v]setsar=1,format=gray,split=3[my][mu][mv];"
                                 "[mu]scale=iw/2:ih/2:flags=area[mu2];[mv]scale=iw/2:ih/2:flags=area[mv2];"
                                 "[my][mu2][mv2]mergeplanes=0x001020:yuv420p[studiomask];"
                                 "[padded][studiobg][studiomask]maskedmerge")
            .arg(studio.output.width()).arg(studio.output.height())
            .arg(studio.content.x()).arg(studio.content.y())
            .arg(background).arg(background + 1);
    }
    const QSize framedSize = req.studio.active() ? studio.output : contentSize;
    const QSize finalSize = exportSize(framedSize, req.maxShortSide);
    if (finalSize != framedSize)
        filter += QStringLiteral(",scale=%1:%2:flags=lanczos").arg(finalSize.width()).arg(finalSize.height());
    if (gif)   // one palette for the whole file
        filter += QStringLiteral(",split[ga][gb];[ga]palettegen=stats_mode=diff[gp];"
                                 "[gb][gp]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle");

    // Half the cores decode and encode, a third filter: 2.5x faster than 2 and 1 on 12
    // threads (audit 2026-10-29). ffmpeg runs niced, so the editor stays responsive.
    const int cores = QThread::idealThreadCount();
    const QString threads = QString::number(std::clamp(cores / 2, 2, 8));
    QStringList args = {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-nostdin"), QStringLiteral("-y"),
        QStringLiteral("-nostats"), QStringLiteral("-progress"), QStringLiteral("pipe:1"),
        QStringLiteral("-threads"), threads,
        QStringLiteral("-filter_complex_threads"), QString::number(std::clamp(cores / 3, 1, 4)),
    };
    if (req.trimInMs > 0) {
        args += {
            QStringLiteral("-ss"), QString::number(req.trimInMs / 1000.0, 'f', 3),
        };
    }
    // With pieces, the length only limits what is read; the pieces set the output.
    if (pieces && trimmed)
        args += {QStringLiteral("-t"), QString::number((req.trimOutMs - req.trimInMs) / 1000.0, 'f', 3)};
    const qsizetype sourceAt = args.size();   // decodeOptions go here
    args += {QStringLiteral("-i"), req.inputPath};
    if (overlayVisible)
        args += {QStringLiteral("-i"), overlayPath};
    for (const auto &file : timedFiles)
        args += {QStringLiteral("-i"), file->fileName()};
    if (req.studio.active())
        args += {QStringLiteral("-i"), studioBackground.fileName(), QStringLiteral("-i"), studioMask.fileName()};
    const QStringList inputs = args;
    QStringList maps = {QStringLiteral("-map"), QStringLiteral("[v]")};
    if (sound) maps += {QStringLiteral("-map"), QStringLiteral("[aout]")};
    else if (!gif && !pieces && req.includeAudio) maps += {QStringLiteral("-map"), QStringLiteral("0:a?")};
    maps += {QStringLiteral("-threads:v"), threads};
    QStringList outputs;
    if (trimmed && !pieces) {
        outputs += {
            QStringLiteral("-t"),
            QString::number((req.trimOutMs - req.trimInMs) / 1000.0, 'f', 3),
        };
    }
    outputs += {
        QStringLiteral("-avoid_negative_ts"), QStringLiteral("make_zero"),
        QStringLiteral("-fpsmax"), QString::number(maxFps),
        QStringLiteral("-shortest"),
        actualOutput,
    };

    // A size limit takes the CPU encoder: its rate cap is the one measured to hold.
    const QString hardware = ext != "webm" && !gif && req.capKbps <= 0 && (req.timeoutMs < 0 || req.timeoutMs >= 5000)
        ? hardwareEncoder(ffmpeg) : QString();
    QStringList encoders;
    if (!hardware.isEmpty()) encoders.append(hardware);
    encoders.append(QString()); // CPU fallback also handles unsupported sizes/formats.
    bool gpu = source.gpuDecodable && cudaDecoder(ffmpeg);
    for (qsizetype e = 0; e < encoders.size(); ++e) {
        const QString &encoder = encoders[e];
        if (req.cancelled && req.cancelled()) {
            r.error = QStringLiteral("video export cancelled");
            return r;
        }
        const QStringList device = deviceArgs(encoder);
        const QStringList codecs = encoderCodecArgs(encoder, codecArgs, reencodeAudio);
        const QStringList decoding = !gpu ? inputs : inputs.mid(0, sourceAt) + decodeOptions(true) + inputs.mid(sourceAt);
        QProcess p;
        p.setChildProcessModifier([] { if (::nice(5) == -1) {} });   // below the editor
        p.start(ffmpeg, device + decoding + QStringList{"-filter_complex", decodeHead(gpu, highFps ? maxFps : 0) + filter
            + (device.isEmpty() ? QString() : QStringLiteral(",format=nv12,hwupload"))
            + "[v]" + audioFilter} + maps + codecs + outputs);
        QElapsedTimer quiet;
        quiet.start();
        qint64 lastUs = -1;
        int lastPercent = -2;
        bool stalled = false;
        for (;;) {
            const int remaining = req.timeoutMs < 0 ? 250
                : qBound(0, req.timeoutMs - int(elapsed.elapsed()), 250);
            const bool done = p.waitForFinished(remaining) || p.state() == QProcess::NotRunning;
            while (p.canReadLine()) {
                const QByteArray line = p.readLine().trimmed();
                if (!line.startsWith("out_time_us=")) continue;
                bool ok = false;
                const qint64 us = line.mid(12).toLongLong(&ok);
                if (!ok || us == lastUs) continue;
                lastUs = us;
                quiet.restart();
                const int percent = outputMs > 0 ? int(qBound(qint64(0), us / 10 / outputMs, qint64(99))) : -1;
                if (req.progress && percent != lastPercent) req.progress(percent);
                lastPercent = percent;
            }
            if (done) break;
            if (req.cancelled && req.cancelled()) {
                p.kill();
                p.waitForFinished(5000);
                r.error = QStringLiteral("video export cancelled");
                return r;
            }
            if (req.timeoutMs >= 0 && elapsed.elapsed() >= req.timeoutMs) {
                p.kill();
                p.waitForFinished(5000);
                r.error = QStringLiteral("ffmpeg export timed out");
                return r;
            }
            if (req.stallTimeoutMs >= 0 && quiet.elapsed() >= req.stallTimeoutMs) {
                p.kill();
                p.waitForFinished(5000);
                stalled = true;
                break;
            }
        }
        if (stalled) {
            r.error = QStringLiteral("ffmpeg stopped making progress")
                + (encoder.isEmpty() ? QString() : QStringLiteral(" with ") + encoder);
            if (gpu) { gpu = false; --e; }   // the same encoder again, decoding on the CPU
            continue;
        }
        if (p.error() == QProcess::FailedToStart) {
            r.error = QStringLiteral("cannot start ffmpeg: ") + p.errorString();
            continue;
        }
        if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
            r.error.clear();
            break;
        }
        r.error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (r.error.isEmpty()) r.error = QStringLiteral("ffmpeg failed");
        if (gpu) { gpu = false; --e; }
    }
    if (!r.error.isEmpty()) return r;
    return finishOutput(replaceInput, actualOutput, req.outputPath);
}

// C7: one try at the rate that fits the budget (measured to hold: 2 MB of noise came out at
// 1.92 MB), one more a step lower if it still does not, else an honest failure.
static DeliverResult writeWithin(const VideoExportRequest &req, bool render) {
    const QString ext = QFileInfo(req.outputPath).suffix().toLower();
    if (req.maxBytes <= 0 || ext == QStringLiteral("gif")) return writeVideo(req, render);
    qint64 sourceMs = req.trimOutMs >= 0 ? req.trimOutMs : probeVideoFile(req.inputPath).info.durationMs;
    const double seconds = std::max(0.1, TimeMap(sourceMs, req.trimInMs, sourceMs, req.fragments).outputDurationMs() / 1000.0);
    const double audioBytes = req.includeAudio ? (ext == QStringLiteral("webm") ? 64000.0 : 96000.0) / 8 * seconds : 0;
    VideoExportRequest attempt = req;
    attempt.capKbps = std::max(100, int((0.97 * req.maxBytes - audioBytes) * 8 / (seconds + 1) / 1000));
    for (int tries = 0; tries < 2; ++tries) {
        DeliverResult r = writeVideo(attempt, render);
        if (!r.ok) return r;
        const qint64 size = QFileInfo(req.outputPath).size();
        if (size <= req.maxBytes) return r;
        attempt.capKbps = std::max(50, int(attempt.capKbps * double(req.maxBytes) / size * 0.95));
    }
    QFile::remove(req.outputPath);
    DeliverResult r;
    r.error = QStringLiteral("Could not fit into %1 MB").arg(req.maxBytes / 1000000.0, 0, 'g', 3);
    return r;
}

DeliverResult writeVideoWithOverlay(const VideoExportRequest &req) {
    // A base view that follows the pointer moves over time like a zoom does.
    const bool follows = req.baseFollowsCursor && req.cursorTrack && !req.baseView.isNull();
    return writeWithin(req, !req.zooms.isEmpty() || follows);
}

DeliverResult writeVideoRendered(const VideoExportRequest &req) {
    return writeWithin(req, true);
}

}
