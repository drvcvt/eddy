#pragma once
#include <QObject>
#include <QImage>
#include <QCache>
#include <QProcess>
#include <QTimer>
#include <QVector>
#include <optional>

namespace eddy {

// One decoder and a bounded in-memory cache for filmstrip and hover images.
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
    Request request(qint64 time, QSize size, bool hover) const;
    void startNext();
    void finish(bool success);
    QString m_source, m_executable;
    QProcess *m_process;
    QTimer m_hoverDelay, m_timeout;
    QCache<QString, QImage> m_cache{32 * 1024}; // KiB
    QVector<Request> m_strip;
    std::optional<Request> m_hover, m_active;
    QByteArray m_bytes;
    bool m_hoverPending = false;
};

}
