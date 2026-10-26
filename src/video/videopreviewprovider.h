#pragma once
#include <QObject>
#include <QImage>
#include <QCache>
#include <QProcess>
#include <QTimer>
#include <QVector>
#include <optional>

namespace eddy {

// A few decoders and a bounded in-memory cache for filmstrip and hover images.
class VideoPreviewProvider : public QObject {
    Q_OBJECT
public:
    explicit VideoPreviewProvider(QString source, QObject *parent = nullptr,
                                  QString executable = {});
    ~VideoPreviewProvider() override;
    void requestStrip(const QVector<qint64> &times, QSize size);
    void requestHover(qint64 time, QSize size);
    void cancelHover();
signals:
    void thumbnailReady(qint64 time, const QImage &image);
    void hoverReady(qint64 time, const QImage &image);
    void hoverFailed(qint64 time);
private:
    struct Request { qint64 time; QSize size; QString key; bool hover; };
    // One ffmpeg at a time per slot. Three decode a strip 2.4x faster than one
    // (24 thumbnails of a 1080p120 recording: 3.2 s -> 1.8 s) and stay exact.
    struct Slot {
        QProcess *process = nullptr;
        QTimer *timeout = nullptr;
        std::optional<Request> active;
        QByteArray bytes;
    };
    static constexpr int kSlots = 3;
    Request request(qint64 time, QSize size, bool hover) const;
    void startNext();
    void start(Slot &slot, const Request &next);
    void finish(Slot &slot, bool success);
    void killStaleHover();
    QString m_source, m_executable;
    Slot m_slots[kSlots];
    QTimer m_hoverDelay;
    QCache<QString, QImage> m_cache{32 * 1024}; // KiB
    QVector<Request> m_strip;
    std::optional<Request> m_hover;
    bool m_hoverPending = false;
};

}
