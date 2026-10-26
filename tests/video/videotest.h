#pragma once
// Helpers shared by the tests split out of test_videoexporter.cpp.
#include <QtTest>
#include <QImage>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "video/mediaio.h"
#include "video/videoexporter.h"
#include "studio/studiostyle.h"

using namespace eddy;

inline bool have(const QString &cmd) {
    return !QStandardPaths::findExecutable(cmd).isEmpty();
}

inline bool runProcess(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(20000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

inline QByteArray processOutput(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(20000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return p.readAllStandardOutput().trimmed();
}

// Binary output as it came, for raw audio.
inline QByteArray rawOutput(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(20000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return p.readAllStandardOutput();
}

inline double horizontalContrast(const QImage &image, const QRect &rect) {
    qint64 total = 0;
    qint64 samples = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x < rect.right(); ++x) {
            total += qAbs(qGray(image.pixel(x, y)) - qGray(image.pixel(x + 1, y)));
            ++samples;
        }
    }
    return samples > 0 ? double(total) / double(samples) : 0.0;
}

