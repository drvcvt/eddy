#include "videoexporter.h"
#include "exporter.h"
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

static bool hasVisiblePixels(const QImage &image) {
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x)
            if (qAlpha(row[x])) return true;
    }
    return false;
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

DeliverResult writeVideoWithOverlay(const VideoExportRequest &req) {
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
    const bool trimmed = req.trimOutMs >= 0;
    if (req.trimInMs < 0 || (trimmed && req.trimOutMs <= req.trimInMs)) {
        r.error = QStringLiteral("invalid video trim range");
        return r;
    }

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        r.error = QStringLiteral("ffmpeg not found");
        return r;
    }

    const bool overlayVisible = hasVisiblePixels(req.overlay);
    QTemporaryFile overlayTmp(QDir::tempPath() + QStringLiteral("/eddy-overlay-XXXXXX.png"));
    if (overlayVisible) {
        if (!overlayTmp.open()) {
            r.error = QStringLiteral("cannot create temporary overlay");
            return r;
        }
        const QByteArray png = encodePng(req.overlay);
        if (png.isEmpty() || overlayTmp.write(png) != png.size()) {
            r.error = QStringLiteral("cannot write temporary overlay");
            return r;
        }
        overlayTmp.close();
    }
    const QString overlayPath = overlayTmp.fileName();

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
    QStringList codecArgs;
    if (ext == QStringLiteral("webm")) {
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
            QStringLiteral("-c:a"), trimmed ? QStringLiteral("aac") : QStringLiteral("copy"),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        };
        if (trimmed)
            codecArgs += {QStringLiteral("-b:a"), QStringLiteral("192k")};
    }

    constexpr int videoBlurRadius = 12;
    // ffmpeg autorotates before the filter graph. Normalize sample aspect ratio
    // to the same display-pixel coordinate system used by the editor.
    QString filter = QStringLiteral("[0:v]scale=%1:%2,setsar=1[base];")
        .arg(req.overlay.width()).arg(req.overlay.height());
    QString current = QStringLiteral("[base]");
    int blurIndex = 0;
    for (const QRect &requested : req.blurRects) {
        const QRect rect = requested.intersected(req.overlay.rect());
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
        filter += base + blurred + QStringLiteral("overlay=%1:%2:format=auto")
            .arg(rect.x()).arg(rect.y()) + next + QStringLiteral(";");
        current = next;
        ++blurIndex;
    }
    filter += current + (overlayVisible ? QStringLiteral("[1:v]overlay=0:0:format=auto:shortest=1")
                                       : QStringLiteral("null"));
    if (!crop.isNull())
        filter += QStringLiteral(",crop=%1:%2:%3:%4").arg(crop.width()).arg(crop.height()).arg(crop.x()).arg(crop.y());

    QStringList args = {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-nostdin"), QStringLiteral("-y"),
        QStringLiteral("-threads"), QStringLiteral("2"),
        QStringLiteral("-filter_complex_threads"), QStringLiteral("1"),
    };
    if (req.trimInMs > 0) {
        args += {
            QStringLiteral("-ss"), QString::number(req.trimInMs / 1000.0, 'f', 3),
        };
    }
    args += {QStringLiteral("-i"), req.inputPath};
    if (overlayVisible)
        args += {QStringLiteral("-loop"), QStringLiteral("1"), QStringLiteral("-i"), overlayPath};
    const QStringList inputs = args;
    const QStringList maps = {
        QStringLiteral("-map"), QStringLiteral("[v]"),
        QStringLiteral("-map"), QStringLiteral("0:a?"),
        QStringLiteral("-threads:v"), QStringLiteral("2"),
    };
    QStringList outputs;
    if (trimmed) {
        outputs += {
            QStringLiteral("-t"),
            QString::number((req.trimOutMs - req.trimInMs) / 1000.0, 'f', 3),
        };
    }
    outputs += {
        QStringLiteral("-avoid_negative_ts"), QStringLiteral("make_zero"),
        QStringLiteral("-fpsmax"), QStringLiteral("60"),
        QStringLiteral("-shortest"),
        actualOutput,
    };

    const QString hardware = ext != "webm" && (req.timeoutMs < 0 || req.timeoutMs >= 5000)
        ? hardwareEncoder(ffmpeg) : QString();
    QStringList encoders;
    if (!hardware.isEmpty()) encoders.append(hardware);
    encoders.append(QString()); // CPU fallback also handles unsupported sizes/formats.
    for (const QString &encoder : encoders) {
        const QStringList device = deviceArgs(encoder);
        QStringList codecs = codecArgs;
        if (!encoder.isEmpty()) {
            codecs = {"-c:v", encoder, "-bf", "0",
                encoder.endsWith("_vaapi") ? "-global_quality" : "-qp", "18",
                "-c:a", trimmed ? "aac" : "copy", "-movflags", "+faststart"};
            if (device.isEmpty()) codecs += {"-pix_fmt", "yuv420p"};
            if (trimmed) codecs += {"-b:a", "192k"};
        }
        QProcess p;
        p.start(ffmpeg, device + inputs + QStringList{"-filter_complex", filter
            + (device.isEmpty() ? QString() : QStringLiteral(",format=nv12,hwupload"))
            + "[v]"} + maps + codecs + outputs);
        const int remaining = req.timeoutMs < 0 ? -1 : qMax(0, req.timeoutMs - int(elapsed.elapsed()));
        if (!p.waitForFinished(remaining)) {
            p.kill();
            p.waitForFinished(5000);
            r.error = QStringLiteral("ffmpeg export timed out");
            return r;
        }
        if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
            r.error.clear();
            break;
        }
        r.error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (r.error.isEmpty()) r.error = QStringLiteral("ffmpeg failed");
    }
    if (!r.error.isEmpty()) return r;

    if (replaceInput) {
        auto renamed = replaceFileAtomically(actualOutput, req.outputPath);
        if (!renamed.ok) {
            QFile::remove(actualOutput);
            return renamed;
        }
    }

    r.ok = true;
    return r;
}

}
