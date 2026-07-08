#include "boltsnapipc.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace eddy {

namespace {

void appendU32BE(QByteArray &out, quint32 value) {
    out.append(char((value >> 24) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

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

}

QByteArray buildBoltsnapAddFrame(const QByteArray &png, const QString &source) {
    const QJsonObject header{
        {QStringLiteral("cmd"), QStringLiteral("add")},
        {QStringLiteral("source"), source},
    };
    const QByteArray headerBytes = QJsonDocument(header).toJson(QJsonDocument::Compact);

    QByteArray frame;
    frame.reserve(8 + headerBytes.size() + png.size());
    appendU32BE(frame, quint32(headerBytes.size()));
    appendU32BE(frame, quint32(png.size()));
    frame.append(headerBytes);
    frame.append(png);
    return frame;
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

DeliverResult sendPngToBoltsnapShelf(const QByteArray &png, const QString &source) {
    DeliverResult result;
    if (png.isEmpty()) {
        result.error = QStringLiteral("empty PNG payload");
        return result;
    }

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

    const QByteArray frame = buildBoltsnapAddFrame(png, source);
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
}

}
