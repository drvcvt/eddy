#include <QtTest>
#include <QByteArray>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "boltsnapipc.h"
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace eddy;

class TestBoltsnapIpc : public QObject {
    Q_OBJECT
private slots:
    void addFrameCarriesHeaderAndPngPayload() {
        const QByteArray png("\x89PNG\r\n\x1a\npayload", 15);
        const QByteArray frame = buildBoltsnapAddFrame(png, QStringLiteral("eddy"));

        QVERIFY(frame.size() > 8);
        QDataStream s(frame);
        s.setByteOrder(QDataStream::BigEndian);
        quint32 headerLen = 0;
        quint32 payloadLen = 0;
        s >> headerLen >> payloadLen;
        QCOMPARE(payloadLen, quint32(png.size()));
        QVERIFY(headerLen > 0);
        QVERIFY(frame.size() == int(8 + headerLen + payloadLen));

        const QByteArray headerBytes = frame.mid(8, int(headerLen));
        const QJsonObject header = QJsonDocument::fromJson(headerBytes).object();
        QCOMPARE(header.value(QStringLiteral("cmd")).toString(), QStringLiteral("add"));
        QCOMPARE(header.value(QStringLiteral("source")).toString(), QStringLiteral("eddy"));
        QCOMPARE(frame.right(png.size()), png);
    }

    void socketPathUsesRuntimeDirWhenAvailable() {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toLocal8Bit());

        QCOMPARE(boltsnapSocketPath(), runtime.filePath(QStringLiteral("boltsnap.sock")));
    }
    void sendWritesFrameToUnixSocket() {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QString path = runtime.filePath(QStringLiteral("boltsnap.sock"));
        const QByteArray pathBytes = QFile::encodeName(path);

        const int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        QVERIFY(server >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        QVERIFY(pathBytes.size() < int(sizeof(addr.sun_path)));
        std::memcpy(addr.sun_path, pathBytes.constData(), size_t(pathBytes.size()));
        QVERIFY2(::bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0,
                 std::strerror(errno));
        QVERIFY2(::listen(server, 1) == 0, std::strerror(errno));

        qputenv("EDDY_BOLTSNAP_SOCKET", pathBytes);
        const QByteArray png("\x89PNG\r\n\x1a\npayload", 15);
        const DeliverResult result = sendPngToBoltsnapShelf(png, QStringLiteral("eddy-test"));
        qunsetenv("EDDY_BOLTSNAP_SOCKET");
        QVERIFY2(result.ok, qPrintable(result.error));

        const int client = ::accept(server, nullptr, nullptr);
        QVERIFY(client >= 0);
        QByteArray received;
        char buf[1024];
        for (;;) {
            const ssize_t n = ::read(client, buf, sizeof(buf));
            if (n <= 0)
                break;
            received.append(buf, qsizetype(n));
        }
        ::close(client);
        ::close(server);

        QCOMPARE(received, buildBoltsnapAddFrame(png, QStringLiteral("eddy-test")));
    }
};

QTEST_GUILESS_MAIN(TestBoltsnapIpc)
#include "test_boltsnapipc.moc"
