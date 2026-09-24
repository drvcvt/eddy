#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVector>

class QProcess;

namespace eddy {

// Peaks and RMS of a video's first audio stream for the waveform lane
// (21.09. plan 4): ffmpeg streams PCM in the background, the bins fill from
// the start. Stereo stays two channels, so opposite phases still show.
class AudioWaveformProvider : public QObject {
    Q_OBJECT
public:
    enum class State { Loading, Ready, Failed };
    struct Summary {
        float peak = 0;      // largest magnitude, 0 to 1
        float rms = 0;
        bool known = false;  // false while that stretch is not read yet
    };
    static constexpr qint64 kBinMs = 10;
    static constexpr int kMaxBins = 1'000'000;
    static constexpr int kRate = 16000;
    static constexpr int kStallMs = 30000;

    // `offsetMs`: where the audio starts on the video's time axis.
    AudioWaveformProvider(const QString &path, qint64 durationMs, qint64 offsetMs, QObject *parent = nullptr);
    ~AudioWaveformProvider() override;

    State state() const { return m_state; }
    qint64 binMs() const { return m_binMs; }
    // Over [fromMs, toMs) in source time, from the coarsest level that still
    // has at least two bins in the range.
    Summary summary(qint64 fromMs, qint64 toMs) const;
    void cancel();

signals:
    void changed();   // at most every 100 ms while loading, and once at the end

private:
    struct Bin {
        float peak = 0;
        float squares = 0;   // sum of squared samples
        qint32 count = 0;
    };
    void read();
    void consume(const float *samples, qint64 frames);
    void publish();
    void finish(bool ok);
    void stopProcess();

    QProcess *m_process = nullptr;
    QTimer m_stall;
    QElapsedTimer m_sincePublish;
    State m_state = State::Loading;
    qint64 m_binMs = kBinMs;
    qint64 m_offsetMs = 0;
    qint64 m_frames = 0;            // samples per channel read so far
    qint64 m_known = 0;             // base bins complete
    qint64 m_levelsKnown = 0;       // base bins the coarser levels cover
    QByteArray m_pending;           // a partial frame between reads
    QVector<QVector<Bin>> m_levels; // [0] the base bins, each next one 16 times coarser
};

}
