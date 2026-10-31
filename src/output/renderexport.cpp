#include "output/renderexport.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QProcess>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

namespace eddy {

namespace {

constexpr int kPollMs = 250;
constexpr qint64 kQueueBytes = 128LL * 1024 * 1024;   // per queue; bounds 4K memory use

enum class Push { Done, Full, Closed };

// A bounded hand-over of frames between two threads; a null Frame marks the end.
template <typename Frame>
class FrameQueue {
public:
    explicit FrameQueue(qint64 frameBytes)
        : m_capacity(std::max<qint64>(2, kQueueBytes / std::max<qint64>(1, frameBytes))) {}

    Push push(Frame &frame, int waitMs) {
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
    std::optional<Frame> pop(int waitMs) {
        std::unique_lock lock(m_mutex);
        if (!m_ready.wait_for(lock, std::chrono::milliseconds(waitMs),
                              [&] { return m_finished || m_closed || !m_frames.empty(); }))
            return std::nullopt;
        if (m_frames.empty() || m_closed) return Frame();
        Frame frame = std::move(m_frames.front());
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
    std::deque<Frame> m_frames;
    bool m_finished = false, m_closed = false;
};

// What goes to the encoder: the drawn image, or its yuv420p bytes.
struct Encoded {
    QImage image;
    QByteArray yuv;
    bool isNull() const { return image.isNull() && yuv.isNull(); }
    const char *data() const { return yuv.isNull() ? reinterpret_cast<const char *>(image.constBits()) : yuv.constData(); }
    qint64 size() const { return yuv.isNull() ? image.sizeInBytes() : yuv.size(); }
};

qint64 yuvBytes(QSize size) {
    return qint64(size.width()) * size.height() + 2LL * ((size.width() + 1) / 2) * ((size.height() + 1) / 2);
}

// BT.709 limited range (Y 16-235, C 16-240), coefficients x 2^16; chroma from the mean of each
// 2x2 block, as swscale does. A third of the bytes of bgra: the pipe into the encoder was the
// zoom export's floor (export-gpu plan E7).
constexpr int kYR = 11966, kYG = 40254, kYB = 4064;
constexpr int kUR = -6596, kUG = -22189, kUB = 28785;
constexpr int kVR = 28785, kVG = -26145, kVB = -2640;

QByteArray toYuv420(const QImage &rgb) {
    const int w = rgb.width(), h = rgb.height(), cw = (w + 1) / 2, ch = (h + 1) / 2;
    QByteArray out(yuvBytes(rgb.size()), Qt::Uninitialized);
    auto *y = reinterpret_cast<uint8_t *>(out.data());
    uint8_t *u = y + qint64(w) * h, *v = u + qint64(cw) * ch;
    for (int row = 0; row < h; ++row) {
        const uint8_t *s = rgb.constScanLine(row);   // b g r x
        uint8_t *d = y + qint64(row) * w;
        for (int x = 0; x < w; ++x)
            d[x] = uint8_t((kYR * s[4 * x + 2] + kYG * s[4 * x + 1] + kYB * s[4 * x] + (16 << 16) + (1 << 15)) >> 16);
    }
    for (int row = 0; row < ch; ++row) {
        const uint8_t *a = rgb.constScanLine(2 * row);
        const uint8_t *b = 2 * row + 1 < h ? rgb.constScanLine(2 * row + 1) : a;
        uint8_t *du = u + qint64(row) * cw, *dv = v + qint64(row) * cw;
        for (int x = 0; x < cw; ++x) {
            const int l = 8 * x, r = 2 * x + 1 < w ? l + 4 : l;
            const int R = a[l + 2] + a[r + 2] + b[l + 2] + b[r + 2];   // four pixels: x4
            const int G = a[l + 1] + a[r + 1] + b[l + 1] + b[r + 1];
            const int B = a[l] + a[r] + b[l] + b[r];
            du[x] = uint8_t((kUR * R + kUG * G + kUB * B + (512 << 16) + (1 << 17)) >> 18);
            dv[x] = uint8_t((kVR * R + kVG * G + kVB * B + (512 << 16) + (1 << 17)) >> 18);
        }
    }
    return out;
}

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

// ffmpeg below the editor, so it stays responsive while an export runs.
void niced() { if (::nice(5) == -1) {} }   // a failed nice only costs the editor some speed

// Frames go through a pipe of our own, not QProcess's channels: QProcess::write takes a fresh
// buffer for every 7 MB frame, 4 million page faults and 7 s of an export's 7.8 (export-gpu
// plan). The child gets its end as stdin or stdout; neither end leaks into other children,
// and ours does not block, so a stop is seen while ffmpeg hangs.
struct FramePipe {
    int ours = -1, theirs = -1;
    bool open(bool weRead) {
        int fds[2];
        if (::pipe2(fds, O_CLOEXEC) != 0) return false;
        ours = weRead ? fds[0] : fds[1];
        theirs = weRead ? fds[1] : fds[0];
        return ::fcntl(ours, F_SETFL, ::fcntl(ours, F_GETFL) | O_NONBLOCK) == 0;
    }
    static void close(int &fd) {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
    ~FramePipe() { close(ours); close(theirs); }
};

// Waits up to kPollMs for `fd` to take or give data; false on a timeout.
bool ready(int fd, short events) {
    pollfd p{fd, events, 0};
    return ::poll(&p, 1, kPollMs) > 0;
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
    FrameQueue<QImage> decoded(sourceBytes);
    FrameQueue<Encoded> rendered(job.yuvOutput ? yuvBytes(job.outputSize) : outputBytes);
    std::atomic_bool stop = false;
    std::atomic<qint64> written = 0;
    std::atomic_bool encoderDone = false;
    QString decodeError, encodeError;   // each written only by its own thread before join

    std::thread decoder([&] {
        FramePipe pipe;
        if (!pipe.open(true)) {
            decodeError = QStringLiteral("cannot open a pipe for ffmpeg");
            decoded.finish();
            return;
        }
        QProcess process;
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setChildProcessModifier([out = pipe.theirs] { niced(); ::dup2(out, STDOUT_FILENO); });
        process.start(job.ffmpeg, job.decodeArgs);
        FramePipe::close(pipe.theirs);   // the child has it; ours must go for the end to show
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
                if (!ready(pipe.ours, POLLIN)) continue;
                const ssize_t n = ::read(pipe.ours, bits + got, size_t(sourceBytes - got));
                if (n > 0) got += n;
                else if (n == 0 || (errno != EINTR && errno != EAGAIN)) break;   // the end
            }
            if (got < sourceBytes) break;
            Push pushed;
            while ((pushed = decoded.push(frame, kPollMs)) == Push::Full && !stop) {}
            if (pushed != Push::Done) break;
        }
        FramePipe::close(pipe.ours);   // an ffmpeg still writing ends on it
        finishProcess(process, stop);
        if (!stop && !succeeded(process)) decodeError = lastError(process);
        decoded.finish();
    });

    std::thread encoder([&] {
        FramePipe pipe;
        if (!pipe.open(false)) {
            encodeError = QStringLiteral("cannot open a pipe for ffmpeg");
            rendered.close();
            encoderDone = true;
            return;
        }
        QProcess process;
        process.setStandardInputFile(QProcess::nullDevice());
        process.setChildProcessModifier([in = pipe.theirs] { niced(); ::dup2(in, STDIN_FILENO); });
        process.start(job.ffmpeg, job.encodeArgs);
        FramePipe::close(pipe.theirs);
        if (!process.waitForStarted()) {
            encodeError = QStringLiteral("cannot start ffmpeg: ") + process.errorString();
            rendered.close();
            encoderDone = true;
            return;
        }
        // An ffmpeg that quit makes write() fail with EPIPE; the signal stays on this thread.
        sigset_t broken;
        sigemptyset(&broken);
        sigaddset(&broken, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &broken, nullptr);
        bool gone = false;
        while (!stop && !gone) {
            std::optional<Encoded> frame = rendered.pop(kPollMs);
            if (!frame) continue;
            if (frame->isNull()) break;
            const char *bits = frame->data();
            const qint64 bytes = frame->size();
            qint64 put = 0;
            while (put < bytes && !stop) {
                if (!ready(pipe.ours, POLLOUT)) continue;
                const ssize_t n = ::write(pipe.ours, bits + put, size_t(bytes - put));
                if (n > 0) put += n;
                else if (n < 0 && errno != EINTR && errno != EAGAIN) { gone = true; break; }
            }
            if (!stop && !gone) ++written;
        }
        FramePipe::close(pipe.ours);   // the end of the input for ffmpeg
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

    // Frames are drawn a few at once, as many as are decoded (one thread each), and go to
    // the encoder in order: the drawing was the zoom export's narrowest stage.
    const int batch = std::clamp(int(std::thread::hardware_concurrency()) / 3, 1, 4);
    qint64 index = 0;
    bool aborted = false;
    bool encoderGone = false;   // it closed its queue: nothing more will be written
    bool ended = false;
    while (!aborted && !ended && !encoderGone) {
        if ((aborted = interrupted())) break;
        std::vector<QImage> sources;
        while (int(sources.size()) < batch) {
            std::optional<QImage> source = decoded.pop(sources.empty() ? kPollMs : 0);
            if (!source) break;
            if (source->isNull()) { ended = true; break; }
            sources.push_back(std::move(*source));
        }
        std::vector<Encoded> outputs(sources.size());
        auto draw = [&](size_t i) {
            QImage drawn(job.outputSize, QImage::Format_RGB32);
            job.render(index + qint64(i), sources[i], drawn);
            if (job.yuvOutput) outputs[i].yuv = toYuv420(drawn);
            else outputs[i].image = std::move(drawn);
        };
        {
            std::vector<std::thread> helpers;
            for (size_t i = 1; i < sources.size(); ++i) helpers.emplace_back(draw, i);
            if (!sources.empty()) draw(0);
            for (std::thread &helper : helpers) helper.join();
        }
        index += qint64(sources.size());
        for (Encoded &output : outputs) {
            Push pushed;
            while ((pushed = rendered.push(output, kPollMs)) == Push::Full)
                if ((aborted = interrupted())) break;
            if (aborted) break;
            if (pushed == Push::Closed) { encoderGone = true; break; }
        }
    }
    rendered.finish();
    while (!aborted && !encoderDone) {
        if ((aborted = interrupted())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Cancelled, or the encoder ended early: the decoder must stop too, or it waits on a
    // full queue nobody drains and the export never returns.
    if (aborted || encoderGone) {
        stop = true;
        decoded.close();
    }
    if (aborted) rendered.close();
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
