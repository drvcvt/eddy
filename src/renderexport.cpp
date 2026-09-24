#include "renderexport.h"
#include <QElapsedTimer>
#include <QProcess>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace eddy {

namespace {

constexpr int kPollMs = 250;
constexpr qint64 kQueueBytes = 128LL * 1024 * 1024;   // per queue; bounds 4K memory use

// A bounded hand-over of frames between two threads.
class FrameQueue {
public:
    explicit FrameQueue(qint64 frameBytes)
        : m_capacity(std::max<qint64>(2, kQueueBytes / std::max<qint64>(1, frameBytes))) {}

    enum class Push { Done, Full, Closed };
    Push push(QImage &frame, int waitMs) {
        std::unique_lock lock(m_mutex);
        if (!m_space.wait_for(lock, std::chrono::milliseconds(waitMs),
                              [&] { return m_closed || qint64(m_frames.size()) < m_capacity; }))
            return Push::Full;
        if (m_closed) return Push::Closed;
        m_frames.push_back(std::move(frame));
        m_ready.notify_one();
        return Push::Done;
    }
    // Empty on timeout; a null image once the producer finished or the queue closed.
    std::optional<QImage> pop(int waitMs) {
        std::unique_lock lock(m_mutex);
        if (!m_ready.wait_for(lock, std::chrono::milliseconds(waitMs),
                              [&] { return m_finished || m_closed || !m_frames.empty(); }))
            return std::nullopt;
        if (m_frames.empty() || m_closed) return QImage();
        QImage frame = std::move(m_frames.front());
        m_frames.pop_front();
        m_space.notify_one();
        return frame;
    }
    void finish() {   // the producer is done; the consumer drains what is left
        std::lock_guard lock(m_mutex);
        m_finished = true;
        m_ready.notify_all();
    }
    void close() {    // abandon both ends
        std::lock_guard lock(m_mutex);
        m_closed = true;
        m_ready.notify_all();
        m_space.notify_all();
    }

private:
    const qint64 m_capacity;
    std::mutex m_mutex;
    std::condition_variable m_ready, m_space;
    std::deque<QImage> m_frames;
    bool m_finished = false, m_closed = false;
};

// The last non-empty line ffmpeg wrote to stderr.
QString lastError(QProcess &process) {
    const QList<QByteArray> lines = process.readAllStandardError().trimmed().split('\n');
    return lines.isEmpty() ? QString() : QString::fromUtf8(lines.last()).trimmed();
}

// Waits for `process` to exit, killing it when `stop` turns true.
void finishProcess(QProcess &process, const std::atomic_bool &stop) {
    while (process.state() != QProcess::NotRunning && !stop) process.waitForFinished(kPollMs);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(5000);
    }
}

bool succeeded(const QProcess &process) {
    return process.error() != QProcess::FailedToStart && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
}

}

DeliverResult runRenderPipeline(const RenderPipeline &job) {
    QElapsedTimer elapsed;
    elapsed.start();
    const qint64 sourceBytes = qint64(job.sourceSize.width()) * job.sourceSize.height() * 4;
    const qint64 outputBytes = qint64(job.outputSize.width()) * job.outputSize.height() * 4;
    FrameQueue decoded(sourceBytes), rendered(outputBytes);
    std::atomic_bool stop = false;
    std::atomic<qint64> written = 0;
    std::atomic_bool encoderDone = false;
    QString decodeError, encodeError;   // each written only by its own thread before join

    std::thread decoder([&] {
        QProcess process;
        process.start(job.ffmpeg, job.decodeArgs);
        if (!process.waitForStarted()) {
            decodeError = QStringLiteral("cannot start ffmpeg: ") + process.errorString();
            decoded.finish();
            return;
        }
        while (!stop) {
            QImage frame(job.sourceSize, QImage::Format_RGB32);   // bgra bytes, opaque
            char *bits = reinterpret_cast<char *>(frame.bits());
            qint64 got = 0;
            while (got < sourceBytes && !stop) {
                if (process.bytesAvailable() == 0 && !process.waitForReadyRead(kPollMs)) {
                    if (process.state() == QProcess::NotRunning) break;
                    continue;
                }
                got += process.read(bits + got, sourceBytes - got);
            }
            if (got < sourceBytes) break;
            FrameQueue::Push pushed;
            while ((pushed = decoded.push(frame, kPollMs)) == FrameQueue::Push::Full && !stop) {}
            if (pushed != FrameQueue::Push::Done) break;
        }
        finishProcess(process, stop);
        if (!stop && !succeeded(process)) decodeError = lastError(process);
        decoded.finish();
    });

    std::thread encoder([&] {
        QProcess process;
        process.start(job.ffmpeg, job.encodeArgs);
        if (!process.waitForStarted()) {
            encodeError = QStringLiteral("cannot start ffmpeg: ") + process.errorString();
            rendered.close();
            encoderDone = true;
            return;
        }
        while (!stop) {
            std::optional<QImage> frame = rendered.pop(kPollMs);
            if (!frame) continue;
            if (frame->isNull()) break;
            process.write(reinterpret_cast<const char *>(frame->constBits()), frame->sizeInBytes());
            while (process.bytesToWrite() > 0 && !stop && process.state() != QProcess::NotRunning)
                process.waitForBytesWritten(kPollMs);
            if (process.state() == QProcess::NotRunning) break;
            if (!stop) ++written;
        }
        if (!stop) process.closeWriteChannel();
        finishProcess(process, stop);
        if (!stop && !succeeded(process)) encodeError = lastError(process);
        rendered.close();   // an encoder that quit early must not leave the renderer waiting
        encoderDone = true;
    });

    DeliverResult result;
    qint64 lastWritten = -1;
    QElapsedTimer quiet;
    quiet.start();
    int lastPercent = -2;
    // Cancel, time out and stall checks, run between every wait of the drawing loop.
    auto interrupted = [&]() -> bool {
        const qint64 now = written;
        if (now != lastWritten) {
            lastWritten = now;
            quiet.restart();
            const int percent = job.expectedFrames > 0
                ? int(std::clamp<qint64>(now * 100 / job.expectedFrames, 0, 99)) : -1;
            if (job.progress && percent != lastPercent) job.progress(percent);
            lastPercent = percent;
        }
        if (job.cancelled && job.cancelled()) result.error = QStringLiteral("video export cancelled");
        else if (job.timeoutMs >= 0 && elapsed.elapsed() >= job.timeoutMs)
            result.error = QStringLiteral("ffmpeg export timed out");
        else if (job.stallTimeoutMs >= 0 && quiet.elapsed() >= job.stallTimeoutMs)
            result.error = QStringLiteral("ffmpeg stopped making progress");
        return !result.error.isEmpty();
    };

    qint64 index = 0;
    bool aborted = false;
    while (!aborted) {
        if ((aborted = interrupted())) break;
        std::optional<QImage> source = decoded.pop(kPollMs);
        if (!source) continue;
        if (source->isNull()) break;
        QImage output(job.outputSize, QImage::Format_RGB32);
        job.render(index++, *source, output);
        FrameQueue::Push pushed;
        while ((pushed = rendered.push(output, kPollMs)) == FrameQueue::Push::Full)
            if ((aborted = interrupted())) break;
        if (pushed == FrameQueue::Push::Closed) break;
    }
    rendered.finish();
    while (!aborted && !encoderDone) {
        if ((aborted = interrupted())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (aborted) {
        stop = true;
        decoded.close();
        rendered.close();
    }
    decoder.join();
    encoder.join();
    if (aborted) return result;
    interrupted();   // the last progress report
    result.error.clear();
    if (!encodeError.isEmpty()) result.error = encodeError;
    else if (!decodeError.isEmpty()) result.error = decodeError;
    else if (index == 0) result.error = QStringLiteral("ffmpeg decoded no frames");
    else if (written < index) result.error = QStringLiteral("ffmpeg stopped before the last frame");
    result.ok = result.error.isEmpty();
    return result;
}

}
