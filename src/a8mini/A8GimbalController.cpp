#include "A8GimbalController.h"
#include <QNetworkDatagram>
#include <cmath>

static QByteArray byte(int value) { return QByteArray(1, char(value)); }
static QByteArray motion(int yaw, int pitch) { return byte(yaw) + byte(pitch); }

A8GimbalController::A8GimbalController(QObject *parent) : QObject(parent) {
    m_continuous.setInterval(50); // 20 Hz; never queue old speed samples on TCP.
    connect(&m_continuous, &QTimer::timeout, this, [this] {
        if (m_yawSpeed || m_pitchSpeed || m_motionStopRepeats > 0) {
            udp(0x07, motion(m_yawSpeed, m_pitchSpeed));
            if (m_motionStopRepeats > 0) --m_motionStopRepeats;
        }
        if (m_zoomDirection || m_zoomStopRepeats > 0) {
            udp(0x05, byte(m_zoomDirection));
            if (m_zoomStopRepeats > 0) --m_zoomStopRepeats;
        }
    });
    m_telemetry.setInterval(200);
    connect(&m_telemetry, &QTimer::timeout, this, [this] {
        udp(0x0d);
        if (m_attitudeValid && m_attitudeAge.elapsed() > 1500) {
            m_attitudeValid = false; emit changed();
        }
    });
    m_heartbeat.setInterval(1000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        if (m_tcpProgress.elapsed() > 3500) { fail("控制回包超时，已停止连续控制"); return; }
        tcp(0x00, byte(0)); tcp(0x0a);
    });
    m_timeout.setSingleShot(true); m_timeout.setInterval(2500);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        // A late ACK has no request sequence to distinguish it from the next
        // action. End this session instead of letting it confirm a later action.
        fail("未收到操作确认，请检查相机状态后重新连接");
    });
    m_connectTimeout.setSingleShot(true); m_connectTimeout.setInterval(4000);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] { fail("控制连接超时"); });
    connect(&m_tcp, &QTcpSocket::connected, this, [this] {
        m_connectTimeout.stop();
        m_address = m_tcp.peerAddress();
        if (!m_udp.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) { fail("无法创建 UDP 控制端口"); return; }
        m_connected = true; m_status = "TCP 已连接，等待读取相机状态"; m_tcpProgress.start();
        m_continuous.start(); m_telemetry.start(); m_heartbeat.start();
        tcp(0x0a); udp(0x0d); emit changed();
    });
    connect(&m_tcp, &QTcpSocket::readyRead, this, [this] {
        m_buffer.append(m_tcp.readAll());
        for (const auto &frame : A8Protocol::takeFrames(m_buffer)) receive(frame, true);
    });
    connect(&m_udp, &QUdpSocket::readyRead, this, [this] {
        while (m_udp.hasPendingDatagrams()) {
            auto datagram = m_udp.receiveDatagram();
            if (datagram.senderAddress() != m_address || datagram.senderPort() != m_port) continue;
            auto bytes = datagram.data();
            for (const auto &frame : A8Protocol::takeFrames(bytes)) receive(frame, false);
        }
    });
    connect(&m_tcp, &QTcpSocket::disconnected, this, [this] {
        if (m_connected) fail("TCP 控制已断开，已停止连续控制");
    });
    connect(&m_tcp, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError) { fail(m_tcp.errorString()); });
}
A8GimbalController::~A8GimbalController() { disconnectControl(); }

void A8GimbalController::reset() {
    m_connected = false;
    m_continuous.stop(); m_telemetry.stop(); m_heartbeat.stop(); m_timeout.stop(); m_connectTimeout.stop();
    m_pending = Pending::None; m_recording = m_mode = -1; m_zoom = 0; m_attitudeValid = false;
    m_yawSpeed = m_pitchSpeed = m_zoomDirection = m_motionStopRepeats = m_zoomStopRepeats = 0;
    m_buffer.clear(); m_udp.close(); m_tcp.abort();
}
void A8GimbalController::connectTo(const QString &host, int port) {
    disconnectControl();
    if (host.trimmed().isEmpty() || port < 1 || port > 65535) {
        m_error = "请填写有效的控制地址和端口"; emit changed(); return;
    }
    m_port = quint16(port); m_error.clear(); m_status = "正在连接控制…";
    m_tcp.connectToHost(host.trimmed(), m_port); m_connectTimeout.start(); emit changed();
}
void A8GimbalController::disconnectControl() {
    stopAll();
    if (m_tcp.state() == QAbstractSocket::ConnectedState) {
        m_tcp.flush(); m_tcp.disconnectFromHost();
        if (m_tcp.state() != QAbstractSocket::UnconnectedState) m_tcp.waitForDisconnected(100);
    }
    reset(); m_status = "控制未连接"; m_error.clear(); emit changed();
}
void A8GimbalController::fail(const QString &message) {
    if (m_failing) return;
    m_failing = true;
    QString detail = busy() ? message + "；" + m_label + "结果未知，未自动重发" : message;
    stopAll(); reset(); m_error = detail; m_status = "控制已断开";
    m_failing = false; emit changed();
}
bool A8GimbalController::tcp(quint8 command, const QByteArray &data) {
    if (!m_connected || m_tcp.state() != QAbstractSocket::ConnectedState) return false;
    auto packet = A8Protocol::encode(command, data, m_sequence++);
    if (m_tcp.bytesToWrite() > 4096 || m_tcp.write(packet) != packet.size()) {
        // Avoid recursively trying to send stop packets on an unusable socket.
        m_connected = false; fail("TCP 发送失败，结果未知"); return false;
    }
    return true;
}
bool A8GimbalController::udp(quint8 command, const QByteArray &data) {
    if (!m_connected) return false;
    auto packet = A8Protocol::encode(command, data, m_sequence++, command != 0x07);
    if (m_udp.writeDatagram(packet, m_address, m_port) != packet.size()) {
        fail("UDP 发送失败，已停止连续控制"); return false;
    }
    return true;
}
void A8GimbalController::setMotion(int yawSpeed, int pitchSpeed) {
    if (!m_connected || yawSpeed < -100 || yawSpeed > 100 || pitchSpeed < -100 || pitchSpeed > 100) return;
    if (busy() && (yawSpeed || pitchSpeed)) return;
    m_yawSpeed = yawSpeed; m_pitchSpeed = pitchSpeed;
    m_motionStopRepeats = 0;
    udp(0x07, motion(yawSpeed, pitchSpeed));
    if (!yawSpeed && !pitchSpeed) { m_motionStopRepeats = 3; tcp(0x07, motion(0, 0)); }
}
void A8GimbalController::setZoom(int direction) {
    if (!m_connected || direction < -1 || direction > 1 || (busy() && direction)) return;
    m_zoomDirection = direction; m_zoomStopRepeats = 0;
    udp(0x05, byte(direction));
    if (!direction) { m_zoomStopRepeats = 3; tcp(0x05, byte(0)); }
}
void A8GimbalController::stopAll() {
    m_yawSpeed = m_pitchSpeed = m_zoomDirection = 0;
    if (!m_connected) return;
    m_motionStopRepeats = m_zoomStopRepeats = 3;
    // Release/stop is a critical discrete command: TCP plus immediate UDP zero.
    udp(0x07, motion(0, 0)); udp(0x05, byte(0));
    tcp(0x07, motion(0, 0)); tcp(0x05, byte(0));
}
void A8GimbalController::action(quint8 command, const QByteArray &data, const QString &label, Pending kind) {
    if (!m_connected || busy()) return;
    stopAll(); m_motionStopRepeats = m_zoomStopRepeats = 0;
    if (!tcp(command, data)) return;
    m_pending = kind; m_pendingCommand = command; m_label = label;
    m_error.clear(); m_status = label + "：已通过 TCP 发送，等待确认"; m_timeout.start();
    if (kind == Pending::Mode || kind == Pending::Record) tcp(0x0a);
    emit changed();
}
void A8GimbalController::center() { action(0x08, byte(1), "回中"); }
void A8GimbalController::pointDown() { setAngles(0, -90); }
void A8GimbalController::setAngles(double yaw, double pitch) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || yaw < -135 || yaw > 135 || pitch < -90 || pitch > 25) {
        m_error = "角度超出 A8 mini 范围：偏航 -135～135°，俯仰 -90～25°"; emit changed(); return;
    }
    action(0x0e, A8Protocol::angleData(yaw, pitch), "指定角度");
}
void A8GimbalController::setAbsoluteZoom(double multiple) {
    if (!std::isfinite(multiple) || multiple < 1 || multiple > 30) {
        m_error = "变焦协议范围为 1.0～30.0，实际范围由相机决定"; emit changed(); return;
    }
    int tenths = int(std::lround(multiple * 10));
    action(0x0f, byte(tenths / 10) + byte(tenths % 10), "指定变焦倍数");
}
void A8GimbalController::setMode(int mode) {
    if (mode < 0 || mode > 2 || busy()) return;
    m_target = mode; action(0x0c, byte(mode + 3), "切换云台模式", Pending::Mode);
}
void A8GimbalController::takePhoto() { action(0x0c, byte(0), "拍照", Pending::Photo); }
void A8GimbalController::toggleRecording() {
    if (busy()) return;
    if (m_recording < 0 || m_recording > 1 || !m_systemAge.isValid() || m_systemAge.elapsed() > 2000) {
        m_error = "录像状态未知或 TF 卡异常，请等待读取最新状态"; emit changed(); return;
    }
    m_target = m_recording == 1 ? 0 : 1;
    action(0x0c, byte(2), m_target ? "开始录像" : "停止录像", Pending::Record);
}
void A8GimbalController::finish(const QString &message, bool error) {
    m_pending = Pending::None; m_timeout.stop(); m_status = message;
    m_error = error ? message : QString(); emit changed();
}
void A8GimbalController::receive(const A8Protocol::Frame &frame, bool fromTcp) {
    if (!(frame.control & 2)) return;
    const auto &data = frame.data;
    if (fromTcp) m_tcpProgress.restart();
    if (frame.command == 0x0a && data.size() >= 7) {
        m_recording = quint8(data[3]); m_mode = quint8(data[4]); m_systemAge.restart();
        if (m_status == "TCP 已连接，等待读取相机状态") m_status = "相机控制就绪";
        if (fromTcp && m_pending == Pending::Mode && m_mode == m_target) finish("云台模式已确认");
        if (fromTcp && m_pending == Pending::Record && m_recording == m_target) finish(m_target ? "已确认开始录像" : "已确认停止录像");
        if (fromTcp && m_pending == Pending::Record && m_recording > 1) finish("录像失败，请检查 TF 卡", true);
    }
    if ((frame.command == 0x0d && data.size() >= 12) || (frame.command == 0x0e && data.size() >= 6)) {
        m_yaw = A8Protocol::signed16(data, 0) / 10.0;
        m_pitch = A8Protocol::signed16(data, 2) / 10.0;
        m_roll = A8Protocol::signed16(data, 4) / 10.0;
        m_attitudeValid = true; m_attitudeAge.restart();
    }
    if (frame.command == 0x05 && data.size() >= 2)
        m_zoom = quint16(A8Protocol::signed16(data, 0)) / 10.0;
    // A8 mini replies use the camera's sequence counter, not the request's.
    // Only one action is in flight; match its command and validate its payload.
    if (fromTcp && m_pending == Pending::Ack && frame.command == m_pendingCommand) {
        if (frame.command == 0x0e && data.size() >= 6) finish(m_label + "已接收，正在执行");
        else if ((frame.command == 0x08 || frame.command == 0x0f) && data.size() == 1)
            finish(m_label + (quint8(data[0]) == 1 ? "已确认" : "失败"), quint8(data[0]) != 1);
    }
    if (fromTcp && frame.command == 0x0b && data.size() == 1) {
        int feedback = quint8(data[0]);
        if (m_pending == Pending::Photo && (feedback == 0 || feedback == 1))
            finish(feedback == 0 ? "拍照成功" : "拍照失败，请检查 TF 卡", feedback == 1);
        if (m_pending == Pending::Record && feedback == 4) finish("录像失败，请检查 TF 卡", true);
    }
    emit changed();
}
