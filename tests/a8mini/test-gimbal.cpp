#include "A8GimbalController.h"
#include <QtTest>
#include <QTcpServer>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <memory>

class GimbalTests : public QObject {
    Q_OBJECT
    QTcpServer server;
    QUdpSocket datagrams;
    QTcpSocket *peer = nullptr;
    QByteArray buffer;
    QVector<A8Protocol::Frame> tcpFrames, udpFrames;
    std::unique_ptr<A8GimbalController> controller;
    void reply(quint8 command, const QByteArray &data) {
        // Camera sequences are intentionally unrelated to the request sequence.
        auto bytes = A8Protocol::encode(command, data, 45000);
        bytes[2] = '\x02';
        auto crc = A8Protocol::crc16(bytes.left(bytes.size() - 2));
        bytes[bytes.size() - 2] = char(crc); bytes[bytes.size() - 1] = char(crc >> 8);
        peer->write(bytes);
    }
    static int count(const QVector<A8Protocol::Frame> &frames, quint8 command, const QByteArray &data) {
        int result = 0;
        for (const auto &frame : frames) if (frame.command == command && frame.data == data) ++result;
        return result;
    }
private slots:
    void init() {
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QVERIFY(datagrams.bind(QHostAddress::LocalHost, server.serverPort()));
        connect(&server, &QTcpServer::newConnection, this, [this] {
            peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, this, [this] {
                buffer.append(peer->readAll());
                tcpFrames += A8Protocol::takeFrames(buffer);
            });
        });
        connect(&datagrams, &QUdpSocket::readyRead, this, [this] {
            while (datagrams.hasPendingDatagrams()) {
                auto bytes = datagrams.receiveDatagram().data();
                udpFrames += A8Protocol::takeFrames(bytes);
            }
        });
        controller = std::make_unique<A8GimbalController>();
        controller->connectTo("127.0.0.1", server.serverPort());
        QTRY_VERIFY(controller->connected() && peer);
    }
    void cleanup() {
        controller.reset();
        if (peer) { peer->disconnect(this); delete peer; peer = nullptr; }
        server.disconnect(this); datagrams.disconnect(this);
        server.close(); datagrams.close(); buffer.clear(); tcpFrames.clear(); udpFrames.clear();
    }
    void manualVectors() {
        QCOMPARE(A8Protocol::encode(0x00, QByteArray::fromHex("00"), 0).toHex(), QByteArray("556601010000000000598b"));
        QCOMPARE(A8Protocol::encode(0x07, QByteArray::fromHex("6464"), 0).toHex(), QByteArray("556601020000000764643dcf"));
        QCOMPARE(A8Protocol::encode(0x08, QByteArray::fromHex("01"), 0).toHex(), QByteArray("556601010000000801d112"));
        QCOMPARE(A8Protocol::encode(0x0c, QByteArray::fromHex("02"), 0).toHex(), QByteArray("556601010000000c0276ee"));
        QCOMPARE(A8Protocol::encode(0x0e, A8Protocol::angleData(0, -90), 0).toHex(), QByteArray("556601040000000e00007cfc4fa4"));
    }
    void fragmentedAndCorruptFrames() {
        auto packet = A8Protocol::encode(0x08, QByteArray(1, 1), 42);
        QByteArray bytes("noise"); bytes.append(packet.left(7));
        QVERIFY(A8Protocol::takeFrames(bytes).isEmpty());
        bytes.append(packet.mid(7)); bytes.append(packet);
        auto decoded = A8Protocol::takeFrames(bytes);
        QCOMPARE(decoded.size(), 2); QCOMPARE(decoded[0].sequence, quint16(42)); QVERIFY(bytes.isEmpty());
        auto corrupt = packet; corrupt[8] = '\x05';
        bytes = corrupt + packet;
        QCOMPARE(A8Protocol::takeFrames(bytes).size(), 1);
    }
    void continuousUsesUdpAndReleaseUsesTcp() {
        controller->setMotion(-20, 30); controller->setZoom(1);
        QTRY_VERIFY(count(udpFrames, 0x07, QByteArray::fromHex("ec1e")) >= 2);
        QCOMPARE(count(tcpFrames, 0x07, QByteArray::fromHex("ec1e")), 0);
        QCOMPARE(count(tcpFrames, 0x05, QByteArray::fromHex("01")), 0);
        controller->setMotion(0, 0); controller->setZoom(0);
        QTRY_VERIFY(count(tcpFrames, 0x07, QByteArray::fromHex("0000")) > 0);
        QTRY_VERIFY(count(tcpFrames, 0x05, QByteArray::fromHex("00")) > 0);
        QTRY_VERIFY(count(udpFrames, 0x07, QByteArray::fromHex("0000")) >= 3);
        int samples = count(udpFrames, 0x07, QByteArray::fromHex("ec1e"));
        QTest::qWait(150);
        QCOMPARE(count(udpFrames, 0x07, QByteArray::fromHex("ec1e")), samples);
    }
    void discreteTcpAndAckValidation() {
        controller->center(); QVERIFY(controller->busy());
        QTRY_COMPARE(count(tcpFrames, 0x08, QByteArray(1, 1)), 1);
        QCOMPARE(count(udpFrames, 0x08, QByteArray(1, 1)), 0);
        reply(0x08, QByteArray()); QTest::qWait(30); QVERIFY(controller->busy());
        reply(0x08, QByteArray(1, 1)); QTRY_VERIFY(!controller->busy());
        QVERIFY(controller->status().contains("已确认"));
        controller->setAngles(60.5, -12.3);
        QTRY_COMPARE(count(tcpFrames, 0x0e, QByteArray::fromHex("5d0285ff")), 1);
        reply(0x0e, QByteArray::fromHex("5d0285ff0000"));
        QTRY_VERIFY(!controller->busy()); QCOMPARE(controller->yaw(), 60.5); QCOMPARE(controller->pitch(), -12.3);
    }
    void boundsAndSingleAction() {
        controller->setAngles(136, 0); controller->setAngles(0, -91);
        controller->setAngles(qQNaN(), 0); controller->setAbsoluteZoom(31);
        controller->setMotion(101, 0); controller->setZoom(2);
        QTest::qWait(70);
        for (const auto &frame : tcpFrames) QVERIFY(frame.command != 0x0e && frame.command != 0x0f);
        controller->center(); controller->takePhoto();
        QTest::qWait(70);
        QCOMPARE(count(tcpFrames, 0x0c, QByteArray(1, 0)), 0);
    }
    void statusConfirmsModeAndRecording() {
        reply(0x0a, QByteArray::fromHex("00000000010100"));
        QTRY_COMPARE(controller->recording(), 0);
        controller->toggleRecording();
        QTRY_COMPARE(count(tcpFrames, 0x0c, QByteArray(1, 2)), 1);
        reply(0x0a, QByteArray::fromHex("00000000010100")); QTest::qWait(30); QVERIFY(controller->busy());
        reply(0x0a, QByteArray::fromHex("00000001010100")); QTRY_VERIFY(!controller->busy());
        QCOMPARE(controller->recording(), 1);
        controller->setMode(2);
        QTRY_COMPARE(count(tcpFrames, 0x0c, QByteArray(1, 5)), 1);
        reply(0x0a, QByteArray::fromHex("00000001020100")); QTRY_VERIFY(!controller->busy());
        QCOMPARE(controller->mode(), 2);
    }
    void missingAckNeverReplaysToggle() {
        controller->takePhoto();
        QTRY_COMPARE(count(tcpFrames, 0x0c, QByteArray(1, 0)), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->connected(), 3500);
        QVERIFY(controller->lastError().contains("结果未知"));
        QCOMPARE(count(tcpFrames, 0x0c, QByteArray(1, 0)), 1);
    }
    void lostTcpStopsMotion() {
        controller->setMotion(20, 0);
        QTRY_VERIFY(count(udpFrames, 0x07, QByteArray::fromHex("1400")) > 0);
        peer->abort(); QTRY_VERIFY(!controller->connected());
        QTRY_VERIFY(count(udpFrames, 0x07, QByteArray::fromHex("0000")) > 0);
    }
    void liveStatusReadOnly() {
        auto host = qgetenv("A8_LIVE_CONTROL_HOST");
        if (host.isEmpty()) QSKIP("Set A8_LIVE_CONTROL_HOST for read-only hardware verification");
        controller->connectTo(QString::fromUtf8(host));
        QTRY_VERIFY_WITH_TIMEOUT(controller->connected(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(controller->mode() >= 0, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(controller->attitudeValid(), 5000);
        qInfo() << "Live camera mode" << controller->mode() << "recording" << controller->recording()
                << "yaw" << controller->yaw() << "pitch" << controller->pitch();
    }
};
QTEST_GUILESS_MAIN(GimbalTests)
#include "test-gimbal.moc"
