#include <QtTest>
#include <QByteArray>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include "boltsnapipc.h"
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <chrono>

using namespace eddy;

class TestBoltsnapIpc : public QObject {
    Q_OBJECT
private slots:
    void addFrameCarriesHeaderAndPngPayload() {
        const QByteArray png("\x89PNG\r\n\x1a\npayload", 15);
        const QByteArray frame = buildBoltsnapAddFrame(
            png, QStringLiteral("eddy"), QStringLiteral("DP-3"));

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
        QCOMPARE(header.value(QStringLiteral("output")).toString(), QStringLiteral("DP-3"));
        QCOMPARE(frame.right(png.size()), png);
    }

    void addFrameOmitsEmptyOutputForCompatibility() {
        const QByteArray frame = buildBoltsnapAddFrame(QByteArray("png"), QStringLiteral("eddy"));
        QDataStream s(frame);
        s.setByteOrder(QDataStream::BigEndian);
        quint32 headerLen = 0;
        quint32 payloadLen = 0;
        s >> headerLen >> payloadLen;
        Q_UNUSED(payloadLen);

        const QJsonObject header = QJsonDocument::fromJson(frame.mid(8, int(headerLen))).object();
        QVERIFY(!header.contains(QStringLiteral("output")));
    }

    void imageReplacementCarriesIdAndPng() {
        const QByteArray png("replacement-png");
        const QByteArray frame = buildBoltsnapImageReplaceFrame(42, png);
        QDataStream s(frame);
        s.setByteOrder(QDataStream::BigEndian);
        quint32 headerLen = 0;
        quint32 payloadLen = 0;
        s >> headerLen >> payloadLen;

        const QJsonObject header = QJsonDocument::fromJson(frame.mid(8, int(headerLen))).object();
        QCOMPARE(header.value(QStringLiteral("cmd")).toString(), QStringLiteral("replace"));
        QCOMPARE(header.value(QStringLiteral("id")).toInteger(), qint64(42));
        QCOMPARE(header.value(QStringLiteral("media")).toString(), QStringLiteral("image"));
        QCOMPARE(payloadLen, quint32(png.size()));
        QCOMPARE(frame.right(png.size()), png);
    }

    void videoReplacementCarriesIdAndPath() {
        const QByteArray frame = buildBoltsnapVideoReplaceFrame(
            43, QStringLiteral("/tmp/edited clip.mp4"));
        QDataStream s(frame);
        s.setByteOrder(QDataStream::BigEndian);
        quint32 headerLen = 0;
        quint32 payloadLen = 0;
        s >> headerLen >> payloadLen;

        const QJsonObject header = QJsonDocument::fromJson(frame.mid(8, int(headerLen))).object();
        QCOMPARE(header.value(QStringLiteral("cmd")).toString(), QStringLiteral("replace"));
        QCOMPARE(header.value(QStringLiteral("id")).toInteger(), qint64(43));
        QCOMPARE(header.value(QStringLiteral("media")).toString(), QStringLiteral("video"));
        QCOMPARE(header.value(QStringLiteral("path")).toString(), QStringLiteral("/tmp/edited clip.mp4"));
        QCOMPARE(payloadLen, quint32(0));
    }

    void videoAddCarriesPathAndOwnership() {
        const QByteArray frame = buildBoltsnapVideoAddFrame(
            QStringLiteral("/tmp/eddy clip.mp4"), QStringLiteral("eddy"), true,
            QStringLiteral("DP-2"));
        QDataStream s(frame);
        s.setByteOrder(QDataStream::BigEndian);
        quint32 headerLen = 0, payloadLen = 0;
        s >> headerLen >> payloadLen;
        const QJsonObject header = QJsonDocument::fromJson(frame.mid(8, int(headerLen))).object();
        QCOMPARE(header.value(QStringLiteral("cmd")).toString(), QStringLiteral("add_video"));
        QCOMPARE(header.value(QStringLiteral("path")).toString(), QStringLiteral("/tmp/eddy clip.mp4"));
        QCOMPARE(header.value(QStringLiteral("source")).toString(), QStringLiteral("eddy"));
        QCOMPARE(header.value(QStringLiteral("take_ownership")).toBool(), true);
        QCOMPARE(header.value(QStringLiteral("output")).toString(), QStringLiteral("DP-2"));
        QCOMPARE(payloadLen, quint32(0));
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
    void stalledSocketWriteTimesOut() {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QString path = runtime.filePath(QStringLiteral("boltsnap.sock"));
        const QByteArray pathBytes = QFile::encodeName(path);

        const int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        QVERIFY(server >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, pathBytes.constData(), size_t(pathBytes.size()));
        QVERIFY2(::bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0,
                 std::strerror(errno));
        QVERIFY2(::listen(server, 1) == 0, std::strerror(errno));

        std::thread stalled([server] {
            const int client = ::accept(server, nullptr, nullptr);
            if (client >= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1400));
                ::close(client);
            }
        });
        qputenv("EDDY_BOLTSNAP_SOCKET", pathBytes);
        QElapsedTimer timer;
        timer.start();
        const DeliverResult result = sendPngToBoltsnapShelf(
            QByteArray(32 * 1024 * 1024, 'x'), QStringLiteral("eddy-test"));
        const qint64 elapsed = timer.elapsed();
        qunsetenv("EDDY_BOLTSNAP_SOCKET");
        stalled.join();
        ::close(server);

        QVERIFY(!result.ok);
        QVERIFY2(elapsed < 1300,
                 qPrintable(QStringLiteral("socket write blocked for %1ms").arg(elapsed)));
    }
};

QTEST_GUILESS_MAIN(TestBoltsnapIpc)
#include "test_boltsnapipc.moc"
