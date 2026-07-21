#include "boltsnapipc.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <cstdlib>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace eddy {

namespace {

using Deadline = std::chrono::steady_clock::time_point;
constexpr auto kSocketTimeout = std::chrono::seconds(1);
constexpr auto kResponseTimeout = std::chrono::minutes(2);

void appendU32BE(QByteArray &out, quint32 value) {
    out.append(char((value >> 24) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

bool waitReady(int fd, short events, Deadline deadline) {
    for (;;) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            errno = ETIMEDOUT;
            return false;
        }
        pollfd pfd{fd, events, 0};
        const int ready = ::poll(&pfd, 1, int(remaining.count()));
        if (ready > 0) return (pfd.revents & (events | POLLHUP)) != 0;
        if (ready == 0) {
            errno = ETIMEDOUT;
            return false;
        }
        if (errno != EINTR) return false;
    }
}

bool writeAll(int fd, const char *data, qsizetype size, Deadline deadline) {
    qsizetype off = 0;
    while (off < size) {
        if (std::chrono::steady_clock::now() >= deadline) {
            errno = ETIMEDOUT;
            return false;
        }
        const ssize_t n = ::send(fd, data + off, size_t(size - off), MSG_NOSIGNAL);
        if (n > 0) {
            off += n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waitReady(fd, POLLOUT, deadline)) continue;
        }
        return false;
    }
    return true;
}

bool readAll(int fd, char *data, qsizetype size, Deadline deadline) {
    qsizetype off = 0;
    while (off < size) {
        const ssize_t n = ::recv(fd, data + off, size_t(size - off), 0);
        if (n > 0) { off += n; continue; }
        if (n == 0) { errno = ECONNRESET; return false; }
        if (errno == EINTR) continue;
        if ((errno == EAGAIN || errno == EWOULDBLOCK)
            && waitReady(fd, POLLIN, deadline)) continue;
        return false;
    }
    return true;
}

quint32 readU32BE(const char *data) {
    return (quint32(quint8(data[0])) << 24) | (quint32(quint8(data[1])) << 16)
        | (quint32(quint8(data[2])) << 8) | quint32(quint8(data[3]));
}

DeliverResult readResponse(int fd, Deadline deadline) {
    char lengths[8];
    if (!readAll(fd, lengths, sizeof(lengths), deadline))
        return {false, QStringLiteral("Boltsnap did not acknowledge the video")};
    const quint32 headerSize = readU32BE(lengths);
    const quint32 payloadSize = readU32BE(lengths + 4);
    if (!headerSize || headerSize > 64 * 1024 || payloadSize)
        return {false, QStringLiteral("invalid Boltsnap acknowledgement")};
    QByteArray header(qsizetype(headerSize), Qt::Uninitialized);
    if (!readAll(fd, header.data(), header.size(), deadline))
        return {false, QStringLiteral("incomplete Boltsnap acknowledgement")};
    const QJsonObject response = QJsonDocument::fromJson(header).object();
    if (!response.value(QStringLiteral("ok")).isBool())
        return {false, QStringLiteral("invalid Boltsnap acknowledgement")};
    if (!response.value(QStringLiteral("ok")).toBool())
        return {false, response.value(QStringLiteral("error")).toString(
                           QStringLiteral("Boltsnap rejected the video"))};
    const QString path = response.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return {false, QStringLiteral("Boltsnap acknowledgement has no owned video path")};
    return {true, {}, path};
}

QByteArray buildFrame(const QJsonObject &header, const QByteArray &payload = {}) {
    const QByteArray headerBytes = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.reserve(8 + headerBytes.size() + payload.size());
    appendU32BE(frame, quint32(headerBytes.size()));
    appendU32BE(frame, quint32(payload.size()));
    frame.append(headerBytes);
    frame.append(payload);
    return frame;
}

DeliverResult sendFrame(const QByteArray &frame, bool expectResponse = false) {
    DeliverResult result;
    const QString path = boltsnapSocketPath();
    const QByteArray pathBytes = QFile::encodeName(path);
    if (pathBytes.size() >= int(sizeof(sockaddr_un::sun_path))) {
        result.error = QStringLiteral("Boltsnap socket path is too long: ") + path;
        return result;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        result.error = QStringLiteral("cannot create Boltsnap socket");
        return result;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, pathBytes.constData(), size_t(pathBytes.size()));
    const Deadline deadline = std::chrono::steady_clock::now() + kSocketTimeout;
    int connectError = 0;
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        if ((errno == EINPROGRESS || errno == EAGAIN) && waitReady(fd, POLLOUT, deadline)) {
            socklen_t errorSize = sizeof(connectError);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &connectError, &errorSize) != 0)
                connectError = errno;
        } else {
            connectError = errno;
        }
    }
    if (connectError != 0) {
        ::close(fd);
        result.error = QStringLiteral("cannot connect to Boltsnap shelf at ")
            + path + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(connectError));
        return result;
    }
    if (!writeAll(fd, frame.constData(), frame.size(), deadline)) {
        const int savedErrno = errno;
        ::close(fd);
        result.error = QStringLiteral("cannot write to Boltsnap shelf: ")
            + QString::fromLocal8Bit(std::strerror(savedErrno));
        return result;
    }
    result = expectResponse
        ? readResponse(fd, std::chrono::steady_clock::now() + kResponseTimeout)
        : DeliverResult{true, {}};
    ::close(fd);
    return result;
}

}

QByteArray buildBoltsnapAddFrame(const QByteArray &png, const QString &source,
                                 const QString &output) {
    QJsonObject header{
        {QStringLiteral("cmd"), QStringLiteral("add")},
        {QStringLiteral("source"), source},
    };
    if (!output.isEmpty())
        header.insert(QStringLiteral("output"), output);
    return buildFrame(header, png);
}

QByteArray buildBoltsnapImageReplaceFrame(quint64 id, const QByteArray &png) {
    return buildFrame({
        {QStringLiteral("cmd"), QStringLiteral("replace")},
        {QStringLiteral("id"), qint64(id)},
        {QStringLiteral("media"), QStringLiteral("image")},
    }, png);
}

QByteArray buildBoltsnapVideoReplaceFrame(quint64 id, const QString &path,
                                          bool takeOwnership) {
    return buildFrame({
        {QStringLiteral("cmd"), QStringLiteral("replace")},
        {QStringLiteral("id"), qint64(id)},
        {QStringLiteral("media"), QStringLiteral("video")},
        {QStringLiteral("path"), path},
        {QStringLiteral("take_ownership"), takeOwnership},
    });
}

QByteArray buildBoltsnapVideoAddFrame(const QString &path, const QString &source,
                                      bool takeOwnership, const QString &output) {
    QJsonObject header{
        {QStringLiteral("cmd"), QStringLiteral("add_video")},
        {QStringLiteral("path"), path},
        {QStringLiteral("source"), source},
        {QStringLiteral("take_ownership"), takeOwnership},
    };
    if (!output.isEmpty()) header.insert(QStringLiteral("output"), output);
    return buildFrame(header);
}

QString boltsnapSocketPath() {
    if (const char *overridePath = std::getenv("EDDY_BOLTSNAP_SOCKET");
        overridePath && *overridePath) {
        return QString::fromLocal8Bit(overridePath);
    }
    if (const char *runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime)
        return QDir(QString::fromLocal8Bit(runtime)).filePath(QStringLiteral("boltsnap.sock"));
    return QDir(QDir::tempPath()).filePath(QStringLiteral("boltsnap.sock"));
}

DeliverResult sendPngToBoltsnapShelf(const QByteArray &png, const QString &source,
                                     const QString &output) {
    DeliverResult result;
    if (png.isEmpty()) {
        result.error = QStringLiteral("empty PNG payload");
        return result;
    }

    return sendFrame(buildBoltsnapAddFrame(png, source, output));
}

DeliverResult sendPngToBoltsnapCard(quint64 id, const QByteArray &png) {
    if (!id || png.isEmpty())
        return {false, QStringLiteral("invalid Boltsnap image replacement")};
    return sendFrame(buildBoltsnapImageReplaceFrame(id, png));
}

DeliverResult sendVideoToBoltsnapCard(quint64 id, const QString &path, bool takeOwnership) {
    if (!id || path.isEmpty())
        return {false, QStringLiteral("invalid Boltsnap video replacement")};
    return sendFrame(buildBoltsnapVideoReplaceFrame(id, path, takeOwnership), true);
}

DeliverResult sendVideoToBoltsnapShelf(const QString &path, const QString &source,
                                       bool takeOwnership, const QString &output) {
    const QFileInfo info(path);
    constexpr qint64 maxTemporaryVideoBytes = 2LL * 1024 * 1024 * 1024;
    if (path.isEmpty() || !info.isFile() || info.size() <= 0)
        return {false, QStringLiteral("invalid video for Boltsnap shelf")};
    if (info.size() >= maxTemporaryVideoBytes)
        return {false, QStringLiteral("video exceeds the 2 GiB temporary shelf limit")};
    return sendFrame(buildBoltsnapVideoAddFrame(path, source, takeOwnership, output), true);
}

}
