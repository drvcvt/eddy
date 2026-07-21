#include "videoexporter.h"
#include "exporter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <filesystem>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace eddy {

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
        const std::error_code ec(int(::GetLastError()), std::system_category());
        r.error = QStringLiteral("cannot replace ") + to + QStringLiteral(": ")
                + QString::fromStdString(ec.message());
        return r;
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

    QTemporaryFile overlayTmp(QDir::tempPath() + QStringLiteral("/eddy-overlay-XXXXXX.png"));
    if (!overlayTmp.open()) {
        r.error = QStringLiteral("cannot create temporary overlay");
        return r;
    }
    const QByteArray png = encodePng(req.overlay);
    if (png.isEmpty() || overlayTmp.write(png) != png.size()) {
        r.error = QStringLiteral("cannot write temporary overlay");
        return r;
    }
    const QString overlayPath = overlayTmp.fileName();
    overlayTmp.close();

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
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-c:a"), QStringLiteral("libopus"),
        };
    } else {
        codecArgs = {
            QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-c:a"), trimmed ? QStringLiteral("aac") : QStringLiteral("copy"),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        };
        if (trimmed)
            codecArgs += {QStringLiteral("-b:a"), QStringLiteral("192k")};
    }

    constexpr int videoBlurRadius = 12;
    QString filter;
    QString current = QStringLiteral("[0:v]");
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
    filter += current + QStringLiteral("[1:v]overlay=0:0:format=auto:shortest=1[v]");

    QStringList args = {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-nostdin"), QStringLiteral("-y"),
    };
    if (req.trimInMs > 0) {
        args += {
            QStringLiteral("-ss"), QString::number(req.trimInMs / 1000.0, 'f', 3),
        };
    }
    args += {
        QStringLiteral("-i"), req.inputPath,
        QStringLiteral("-loop"), QStringLiteral("1"),
        QStringLiteral("-i"), overlayPath,
        QStringLiteral("-filter_complex"),
        filter,
        QStringLiteral("-map"), QStringLiteral("[v]"),
        QStringLiteral("-map"), QStringLiteral("0:a?"),
    };
    args += codecArgs;
    if (trimmed) {
        args += {
            QStringLiteral("-t"),
            QString::number((req.trimOutMs - req.trimInMs) / 1000.0, 'f', 3),
        };
    }
    args += {
        QStringLiteral("-avoid_negative_ts"), QStringLiteral("make_zero"),
        QStringLiteral("-fpsmax"), QStringLiteral("60"),
        QStringLiteral("-shortest"),
        actualOutput,
    };

    QProcess p;
    p.start(ffmpeg, args);
    if (!p.waitForFinished(req.timeoutMs)) {
        p.kill();
        p.waitForFinished(5000);
        QFile::remove(overlayPath);
        if (replaceInput) QFile::remove(actualOutput);
        r.error = QStringLiteral("ffmpeg export timed out");
        return r;
    }
    QFile::remove(overlayPath);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (replaceInput) QFile::remove(actualOutput);
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        r.error = err.isEmpty() ? QStringLiteral("ffmpeg failed") : err;
        return r;
    }

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
