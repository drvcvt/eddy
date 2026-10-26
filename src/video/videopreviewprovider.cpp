#include "video/videopreviewprovider.h"
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QBuffer>
#include <QImageReader>
#include <algorithm>

namespace eddy {

VideoPreviewProvider::VideoPreviewProvider(QString source, QObject *parent, QString executable)
    : QObject(parent), m_source(std::move(source)),
      m_executable(executable.isEmpty() ? QStandardPaths::findExecutable("ffmpeg") : executable) {
    m_hoverDelay.setSingleShot(true);
    m_hoverDelay.setInterval(80);
    connect(&m_hoverDelay, &QTimer::timeout, this, [this] { killStaleHover(); startNext(); });
    for (Slot &slot : m_slots) {
        slot.process = new QProcess(this);
        slot.timeout = new QTimer(this);
        slot.timeout->setSingleShot(true);
        slot.timeout->setInterval(5000);
        QProcess *process = slot.process;
        connect(slot.timeout, &QTimer::timeout, process, &QProcess::kill);
        connect(process, &QProcess::readyReadStandardOutput, this, [&slot] {
            slot.bytes += slot.process->readAllStandardOutput();
            if (slot.bytes.size() > 4 * 1024 * 1024) {
                slot.bytes.clear();
                slot.process->kill();
            }
        });
        connect(process, &QProcess::readyReadStandardError, this, [process] {
            process->readAllStandardError(); // Drain diagnostics; no unbounded stderr buffer.
        });
        connect(process, &QProcess::finished, this, [this, &slot](int code, QProcess::ExitStatus status) {
            finish(slot, code == 0 && status == QProcess::NormalExit);
        });
        connect(process, &QProcess::errorOccurred, this, [this, &slot](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(slot, false);
        });
    }
}

VideoPreviewProvider::~VideoPreviewProvider() {
    for (Slot &slot : m_slots) {
        QProcess *process = slot.process;
        if (process->state() == QProcess::NotRunning) continue;
        // Let its own finished signal reap the child without blocking the GUI.
        process->disconnect(this);
        process->setParent(nullptr);
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) process->deleteLater();
        });
        process->kill();
    }
}

VideoPreviewProvider::Request VideoPreviewProvider::request(qint64 time, QSize size, bool hover) const {
    size = size.boundedTo(QSize(640, 360)).expandedTo(QSize(16, 16));
    const QFileInfo source(m_source);
    const QString key = QStringLiteral("%1:%2:%3:%4x%5")
        .arg(source.size()).arg(source.lastModified().toMSecsSinceEpoch())
        .arg(qMax<qint64>(0, time)).arg(size.width()).arg(size.height());
    return {qMax<qint64>(0, time), size, key, hover};
}

void VideoPreviewProvider::requestStrip(const QVector<qint64> &times, QSize size) {
    m_strip.clear();
    for (qint64 time : times.mid(0, 24)) {
        Request next = request(time, size, false);
        if (auto *cached = m_cache.object(next.key)) emit thumbnailReady(time, *cached);
        else m_strip.append(next);
    }
    startNext();
}

void VideoPreviewProvider::requestHover(qint64 time, QSize size) {
    Request next = request(time, size, true);
    if (m_hover && m_hover->key == next.key) return;
    m_hover = next;
    if (auto *cached = m_cache.object(next.key)) {
        m_hoverPending = false;
        m_hoverDelay.stop();
        emit hoverReady(next.time, *cached);
    } else {
        m_hoverPending = true;
        m_hoverDelay.start();
    }
}

void VideoPreviewProvider::cancelHover() {
    m_hover.reset();
    m_hoverPending = false;
    m_hoverDelay.stop();
    killStaleHover();
}

// A hover decode the pointer has moved on from only holds a slot.
void VideoPreviewProvider::killStaleHover() {
    for (Slot &slot : m_slots)
        if (slot.active && slot.active->hover && (!m_hover || slot.active->key != m_hover->key))
            slot.process->kill();
}

void VideoPreviewProvider::startNext() {
    for (Slot &slot : m_slots) {
        if (slot.active) continue;
        const bool hoverRunning = std::any_of(std::begin(m_slots), std::end(m_slots), [this](const Slot &s) {
            return s.active && m_hover && s.active->key == m_hover->key; });
        if (m_hoverPending && !m_hoverDelay.isActive() && !hoverRunning) {
            m_hoverPending = false;
            start(slot, *m_hover);
        } else if (!m_strip.isEmpty()) {
            start(slot, m_strip.takeFirst());
        } else return;
    }
}

void VideoPreviewProvider::start(Slot &slot, const Request &next) {
    slot.active = next;
    slot.bytes.clear();
    const QString filter = QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease")
        .arg(next.size.width()).arg(next.size.height());
    slot.process->start(m_executable, {"-v", "error", "-nostdin", "-ss",
        QString::number(next.time / 1000.0, 'f', 3), "-i", m_source,
        "-an", "-sn", "-frames:v", "1", "-vf", filter, "-threads", "1",
        "-f", "image2pipe", "-vcodec", "png", "pipe:1"});
    slot.timeout->start();
}

void VideoPreviewProvider::finish(Slot &slot, bool success) {
    if (!slot.active) return;
    slot.timeout->stop();
    const Request done = *slot.active;
    slot.active.reset();
    QByteArray bytes = slot.bytes + slot.process->readAllStandardOutput();
    slot.bytes.clear();
    QImage image;
    if (success && bytes.size() <= 4 * 1024 * 1024) {
        QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (size.width() > 0 && size.height() > 0 && size.width() <= 640 && size.height() <= 360)
            image = reader.read();
    }
    if (!image.isNull()) {
        m_cache.insert(done.key, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
        if (m_hover && m_hover->key == done.key) {
            m_hoverPending = false;
            emit hoverReady(done.time, image);
        }
        if (!done.hover) emit thumbnailReady(done.time, image);
    } else if (done.hover && m_hover && m_hover->key == done.key) {
        emit hoverFailed(done.time);
    }
    QTimer::singleShot(0, this, &VideoPreviewProvider::startNext);
}

}
