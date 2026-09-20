#include "videopreviewprovider.h"
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QBuffer>
#include <QImageReader>

namespace eddy {

VideoPreviewProvider::VideoPreviewProvider(QString source, QObject *parent, QString executable)
    : QObject(parent), m_source(std::move(source)),
      m_executable(executable.isEmpty() ? QStandardPaths::findExecutable("ffmpeg") : executable),
      m_process(new QProcess(this)) {
    m_hoverDelay.setSingleShot(true);
    m_hoverDelay.setInterval(80);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    connect(&m_hoverDelay, &QTimer::timeout, this, [this] {
        if (m_active && m_hover && m_active->key != m_hover->key) m_process->kill();
        else startNext();
    });
    connect(&m_timeout, &QTimer::timeout, m_process, &QProcess::kill);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_bytes += m_process->readAllStandardOutput();
        if (m_bytes.size() > 4 * 1024 * 1024) {
            m_bytes.clear();
            m_process->kill();
        }
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_process->readAllStandardError(); // Drain diagnostics; no unbounded stderr buffer.
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        finish(code == 0 && status == QProcess::NormalExit);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false);
    });
}

VideoPreviewProvider::~VideoPreviewProvider() {
    if (m_process->state() != QProcess::NotRunning) {
        // Let its own finished signal reap the child without blocking the GUI.
        m_process->disconnect(this);
        m_process->setParent(nullptr);
        connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);
        m_process->kill();
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
    if (m_active && m_active->hover) m_process->kill();
}

void VideoPreviewProvider::startNext() {
    if (m_active) return;
    if (m_hoverPending && !m_hoverDelay.isActive()) {
        m_active = m_hover;
        m_hoverPending = false;
    } else if (!m_strip.isEmpty()) {
        m_active = m_strip.takeFirst();
    } else return;
    m_bytes.clear();
    const Request &next = *m_active;
    const QString filter = QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease")
        .arg(next.size.width()).arg(next.size.height());
    m_process->start(m_executable, {"-v", "error", "-nostdin", "-ss",
        QString::number(next.time / 1000.0, 'f', 3), "-i", m_source,
        "-an", "-sn", "-frames:v", "1", "-vf", filter, "-threads", "1",
        "-f", "image2pipe", "-vcodec", "png", "pipe:1"});
    m_timeout.start();
}

void VideoPreviewProvider::finish(bool success) {
    if (!m_active) return;
    m_timeout.stop();
    const Request done = *m_active;
    m_active.reset();
    m_bytes += m_process->readAllStandardOutput();
    QImage image;
    if (success && m_bytes.size() <= 4 * 1024 * 1024) {
        QBuffer buffer(&m_bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (size.width() > 0 && size.height() > 0 && size.width() <= 640 && size.height() <= 360)
            image = reader.read();
    }
    m_bytes.clear();
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
