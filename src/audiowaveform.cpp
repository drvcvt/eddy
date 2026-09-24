#include "audiowaveform.h"
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace eddy {

static constexpr int kLevelFactor = 16;
static constexpr int kChannels = 2;

AudioWaveformProvider::AudioWaveformProvider(const QString &path, qint64 durationMs, qint64 offsetMs, QObject *parent)
    : QObject(parent), m_offsetMs(offsetMs) {
    // Very long clips get coarser bins instead of more of them.
    const qint64 duration = std::max<qint64>(1, durationMs);
    m_binMs = kBinMs * std::max<qint64>(1, (duration + kBinMs * kMaxBins - 1) / (kBinMs * kMaxBins));
    qint64 bins = (duration + m_binMs - 1) / m_binMs;
    m_levels.append(QVector<Bin>(bins));
    while (bins > 1) {
        bins = (bins + kLevelFactor - 1) / kLevelFactor;
        m_levels.append(QVector<Bin>(bins));
    }

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        m_state = State::Failed;
        return;
    }
    m_process = new QProcess(this);
    m_process->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_UNIX
    m_process->setChildProcessModifier([] { (void)::nice(10); });   // the editor and exports come first
#endif
    connect(m_process, &QProcess::readyReadStandardOutput, this, &AudioWaveformProvider::read);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        read();
        finish(status == QProcess::NormalExit && code == 0);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false);
    });
    m_stall.setSingleShot(true);
    m_stall.setInterval(kStallMs);
    connect(&m_stall, &QTimer::timeout, this, [this] { finish(false); });
    m_sincePublish.start();
    m_stall.start();
    m_process->start(ffmpeg, {"-v", "error", "-nostdin", "-i", path, "-map", "0:a:0", "-vn",
                              "-ac", QString::number(kChannels), "-ar", QString::number(kRate),
                              "-f", "f32le", "-"});
}

AudioWaveformProvider::~AudioWaveformProvider() {
    cancel();
}

void AudioWaveformProvider::cancel() {
    m_stall.stop();
    if (!m_process) return;
    stopProcess();
    if (m_state == State::Loading) m_state = State::Failed;
}

// A killed ffmpeg is let go and reaped when it has exited; nothing waits for it.
void AudioWaveformProvider::stopProcess() {
    if (!m_process) return;
    QProcess *process = std::exchange(m_process, nullptr);
    process->disconnect(this);
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
        return;
    }
    process->setParent(nullptr);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    process->kill();
}

void AudioWaveformProvider::read() {
    if (!m_process) return;
    m_pending += m_process->readAllStandardOutput();
    if (m_pending.isEmpty()) return;
    m_stall.start();
    constexpr qsizetype frameBytes = qsizetype(sizeof(float)) * kChannels;
    const qsizetype usable = m_pending.size() / frameBytes * frameBytes;
    QVector<float> samples(usable / qsizetype(sizeof(float)));
    std::memcpy(samples.data(), m_pending.constData(), size_t(usable));
    m_pending.remove(0, usable);
    consume(samples.constData(), usable / frameBytes);
    if (m_sincePublish.elapsed() >= 100) publish();
}

void AudioWaveformProvider::consume(const float *samples, qint64 frames) {
    QVector<Bin> &base = m_levels[0];
    for (qint64 i = 0; i < frames; ++i) {
        const qint64 ms = m_offsetMs + (m_frames + i) * 1000 / kRate;
        if (ms < 0) continue;
        const qint64 bin = ms / m_binMs;
        if (bin >= base.size()) break;
        Bin &b = base[bin];
        for (int c = 0; c < kChannels; ++c) {
            const float v = samples[i * kChannels + c];
            if (!std::isfinite(v)) continue;
            b.peak = std::max(b.peak, std::min(1.0f, std::abs(v)));
            b.squares += v * v;
            ++b.count;
        }
    }
    m_frames += frames;
    const qint64 reached = (m_offsetMs + m_frames * 1000 / kRate) / m_binMs;
    m_known = std::clamp<qint64>(reached, m_known, base.size());
}

// Folds the newly complete base bins into the coarser levels.
void AudioWaveformProvider::publish() {
    qint64 from = m_levelsKnown, to = m_known;
    for (int level = 1; level < m_levels.size() && from < to; ++level) {
        const QVector<Bin> &finer = m_levels[level - 1];
        QVector<Bin> &coarse = m_levels[level];
        from /= kLevelFactor;
        to = (to + kLevelFactor - 1) / kLevelFactor;
        for (qint64 j = from; j < to && j < coarse.size(); ++j) {
            Bin sum;
            const qint64 end = std::min<qint64>((j + 1) * kLevelFactor, finer.size());
            for (qint64 k = j * kLevelFactor; k < end; ++k) {
                sum.peak = std::max(sum.peak, finer[k].peak);
                sum.squares += finer[k].squares;
                sum.count += finer[k].count;
            }
            coarse[j] = sum;
        }
    }
    m_levelsKnown = m_known;
    m_sincePublish.restart();
    emit changed();
}

void AudioWaveformProvider::finish(bool ok) {
    if (m_state != State::Loading) return;
    m_stall.stop();
    stopProcess();
    if (ok) m_known = m_levels[0].size();   // after the sound ends there is silence
    m_state = ok ? State::Ready : State::Failed;
    publish();
}

AudioWaveformProvider::Summary AudioWaveformProvider::summary(qint64 fromMs, qint64 toMs) const {
    Summary out;
    const qint64 count = m_levels[0].size();
    const qint64 b0 = std::clamp<qint64>(fromMs / m_binMs, 0, count);
    const qint64 b1 = std::clamp<qint64>(std::max(toMs / m_binMs, b0 + 1), 0, count);
    if (b0 >= b1 || b1 > m_levelsKnown) return out;
    int level = 0;
    qint64 span = 1;
    while (level + 1 < m_levels.size() && (b1 - b0) >= 2 * span * kLevelFactor) {
        ++level;
        span *= kLevelFactor;
    }
    double squares = 0;
    qint64 samples = 0;
    for (qint64 j = b0 / span; j < (b1 + span - 1) / span; ++j) {
        const Bin &b = m_levels[level][j];
        out.peak = std::max(out.peak, b.peak);
        squares += b.squares;
        samples += b.count;
    }
    out.rms = samples > 0 ? float(std::sqrt(squares / samples)) : 0.0f;
    out.known = true;
    return out;
}

}
