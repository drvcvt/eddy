#include "boltsnapipc.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QProcessEnvironment>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifdef Q_OS_WIN
#include <QLocalSocket>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace eddy {

namespace {

void appendU32BE(QByteArray &out, quint32 value) {
    out.append(char((value >> 24) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

#ifndef Q_OS_WIN
bool writeAll(int fd, const char *data, qsizetype size) {
    qsizetype off = 0;
    while (off < size) {
        const ssize_t n = ::write(fd, data + off, size_t(size - off));
        if (n > 0) {
            off += n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}
#endif

QByteArray buildFrame(const QJsonObject &header, const QByteArray &payload) {
    const QByteArray headerBytes = QJsonDocument(header).toJson(QJsonDocument::Compact);

    QByteArray frame;
    frame.reserve(8 + headerBytes.size() + payload.size());
    appendU32BE(frame, quint32(headerBytes.size()));
    appendU32BE(frame, quint32(payload.size()));
    frame.append(headerBytes);
    frame.append(payload);
    return frame;
}

#ifdef Q_OS_WIN
QString windowsPipeUserKey() {
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString domain = env.value(QStringLiteral("USERDOMAIN"));
    const QString user = env.value(QStringLiteral("USERNAME"), QStringLiteral("user"));
    const QString raw = domain + QLatin1Char('-') + user;
    QString key;
    key.reserve(raw.size());
    for (const QChar ch : raw) {
        const ushort value = ch.unicode();
        const bool safe = (value >= 'a' && value <= 'z')
            || (value >= 'A' && value <= 'Z')
            || (value >= '0' && value <= '9')
            || value == '-' || value == '_';
        key += safe ? ch.toLower() : QLatin1Char('_');
    }
    return key;
}

quint32 readU32BE(const QByteArray &data, int offset) {
    const auto *p = reinterpret_cast<const uchar *>(data.constData() + offset);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

bool waitForBytes(QLocalSocket &socket, qsizetype count, int timeoutMs) {
    while (socket.bytesAvailable() < count) {
        if (!socket.waitForReadyRead(timeoutMs))
            return false;
    }
    return true;
}

DeliverResult sendFrameToWindowsPipe(const QByteArray &frame) {
    DeliverResult result;
    QLocalSocket socket;
    socket.connectToServer(boltsnapSocketPath(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(1500)) {
        result.error = QStringLiteral("cannot connect to Boltsnap shelf at ")
            + boltsnapSocketPath() + QStringLiteral(": ") + socket.errorString();
        return result;
    }
    if (socket.write(frame) != frame.size()) {
        result.error = QStringLiteral("cannot write to Boltsnap shelf: ") + socket.errorString();
        return result;
    }
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(2000)) {
            result.error = QStringLiteral("cannot write to Boltsnap shelf: ") + socket.errorString();
            return result;
        }
    }

    if (!waitForBytes(socket, 8, 3000)) {
        result.error = QStringLiteral("Boltsnap shelf did not acknowledge the request: ")
            + socket.errorString();
        return result;
    }
    QByteArray response = socket.read(8);
    const quint32 headerSize = readU32BE(response, 0);
    const quint32 payloadSize = readU32BE(response, 4);
    if (headerSize > 64 * 1024 || payloadSize > 256 * 1024 * 1024) {
        result.error = QStringLiteral("invalid Boltsnap shelf response size");
        return result;
    }
    const qsizetype remaining = qsizetype(headerSize) + qsizetype(payloadSize);
    if (!waitForBytes(socket, remaining, 3000)) {
        result.error = QStringLiteral("incomplete response from Boltsnap shelf");
        return result;
    }
    response += socket.read(remaining);
    const QJsonDocument document = QJsonDocument::fromJson(response.mid(8, int(headerSize)));
    if (!document.isObject()) {
        result.error = QStringLiteral("invalid response from Boltsnap shelf");
        return result;
    }
    const QJsonObject header = document.object();
    if (!header.value(QStringLiteral("ok")).toBool(false)) {
        result.error = header.value(QStringLiteral("error")).toString(
            QStringLiteral("Boltsnap shelf rejected the request"));
        return result;
    }
    result.ok = true;
    return result;
}
#endif

DeliverResult sendFrame(const QByteArray &frame) {
#ifdef Q_OS_WIN
    return sendFrameToWindowsPipe(frame);
#else
    DeliverResult result;
    const QString path = boltsnapSocketPath();
    const QByteArray pathBytes = QFile::encodeName(path);
    if (pathBytes.size() >= int(sizeof(sockaddr_un::sun_path))) {
        result.error = QStringLiteral("Boltsnap socket path is too long: ") + path;
        return result;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        result.error = QStringLiteral("cannot create Boltsnap socket");
        return result;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, pathBytes.constData(), size_t(pathBytes.size()));

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        const int savedErrno = errno;
        ::close(fd);
        result.error = QStringLiteral("cannot connect to Boltsnap shelf at ")
            + path + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(savedErrno));
        return result;
    }

    if (!writeAll(fd, frame.constData(), frame.size())) {
        const int savedErrno = errno;
        ::close(fd);
        result.error = QStringLiteral("cannot write to Boltsnap shelf: ")
            + QString::fromLocal8Bit(std::strerror(savedErrno));
        return result;
    }

    ::close(fd);
    result.ok = true;
    return result;
#endif
}

}

QByteArray buildBoltsnapAddFrame(const QByteArray &png, const QString &source) {
    const QJsonObject header{
        {QStringLiteral("cmd"), QStringLiteral("add")},
        {QStringLiteral("source"), source},
    };
    return buildFrame(header, png);
}

QByteArray buildBoltsnapReloadFrame(quint64 cardId) {
    if (cardId == 0 || cardId > quint64(std::numeric_limits<qint64>::max()))
        return {};
    return buildFrame({
        {QStringLiteral("cmd"), QStringLiteral("reload")},
        {QStringLiteral("id"), qint64(cardId)},
    }, {});
}

QString boltsnapSocketPath() {
    if (const char *overridePath = std::getenv("EDDY_BOLTSNAP_SOCKET");
        overridePath && *overridePath) {
        return QString::fromLocal8Bit(overridePath);
    }
#ifdef Q_OS_WIN
    return QStringLiteral("\\\\.\\pipe\\boltsnap-") + windowsPipeUserKey();
#else
    if (const char *runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime)
        return QDir(QString::fromLocal8Bit(runtime)).filePath(QStringLiteral("boltsnap.sock"));
    return QDir(QDir::tempPath()).filePath(QStringLiteral("boltsnap.sock"));
#endif
}

DeliverResult sendPngToBoltsnapShelf(const QByteArray &png, const QString &source) {
    DeliverResult result;
    if (png.isEmpty()) {
        result.error = QStringLiteral("empty PNG payload");
        return result;
    }

    const QByteArray frame = buildBoltsnapAddFrame(png, source);
    return sendFrame(frame);
}

DeliverResult reloadBoltsnapShelfCard(quint64 cardId) {
    DeliverResult result;
    const QByteArray frame = buildBoltsnapReloadFrame(cardId);
    if (frame.isEmpty()) {
        result.error = QStringLiteral("invalid Boltsnap shelf card id");
        return result;
    }
    return sendFrame(frame);
}

}
