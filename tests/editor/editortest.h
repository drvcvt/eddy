#pragma once
// Helpers shared by the tests split out of test_editorwindow.cpp.
#include <QtTest>
#include "editor/editorwindow.h"
#include "video/mediaio.h"
#include "bars/redactbar.h"
#include "chrome/toast.h"
#include "chrome/dragpill.h"
#include "canvas/canvas.h"
#include "edit/toolcontroller.h"
#include "items/shapes/rectitem.h"
#include "items/labels/textitem.h"
#include "items/regions/redactitem.h"
#include "bars/textbar.h"
#include "bars/spotlightbar.h"
#include "timeline/videotimeline.h"
#include "items/regions/spotlightitem.h"
#include "edit/undocommands.h"
#include "popovers/studiopopover.h"
#include <QUndoStack>
#include <QSlider>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QSlider>
#include <QToolButton>
#include <QLabel>
#include <QLayout>
#include <QFontInfo>
#include <QClipboard>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsVideoItem>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QSettings>
#include <QScopeGuard>
#include <QApplication>
#include <QHelpEvent>
#include <QLineEdit>
#include <QFontDatabase>
#include <QMenu>
#include <QUndoStack>
#include <cstring>
#ifndef Q_OS_WIN
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#endif
#include "app/theme.h"
using namespace eddy;

inline bool have(const QString &cmd) {
    return !QStandardPaths::findExecutable(cmd).isEmpty();
}

inline bool runProcess(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(15000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

#ifndef Q_OS_WIN
inline QByteArray videoResponseFrame(const QString &path) {
    const QByteArray header = QJsonDocument(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("error"), QJsonValue()},
        {QStringLiteral("path"), path},
        {QStringLiteral("snapshot"), QJsonValue()},
    }).toJson(QJsonDocument::Compact);
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << quint32(header.size()) << quint32(0);
    frame.append(header);
    return frame;
}
#endif

inline bool sceneHasVideoItem(const EditorWindow &w) {
    const auto *scene = w.findChild<QGraphicsScene *>();
    if (!scene)
        return false;
    for (QGraphicsItem *item : scene->items()) {
        if (dynamic_cast<QGraphicsVideoItem *>(item))
            return true;
    }
    return false;
}

