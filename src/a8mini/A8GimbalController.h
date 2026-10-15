#pragma once
#include "A8GimbalProtocol.h"
#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>

class A8GimbalController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(double yaw READ yaw NOTIFY changed)
    Q_PROPERTY(double pitch READ pitch NOTIFY changed)
    Q_PROPERTY(double roll READ roll NOTIFY changed)
    Q_PROPERTY(bool attitudeValid READ attitudeValid NOTIFY changed)
    Q_PROPERTY(double zoom READ zoom NOTIFY changed)
    Q_PROPERTY(int recording READ recording NOTIFY changed)
    Q_PROPERTY(int mode READ mode NOTIFY changed)
public:
    explicit A8GimbalController(QObject *parent = nullptr);
    ~A8GimbalController() override;
    bool connected() const { return m_connected; }
    bool busy() const { return m_pending != Pending::None; }
    QString status() const { return m_status; }
    QString lastError() const { return m_error; }
    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    double roll() const { return m_roll; }
    bool attitudeValid() const { return m_attitudeValid; }
    double zoom() const { return m_zoom; }
    int recording() const { return m_recording; }
    int mode() const { return m_mode; }
    Q_INVOKABLE void connectTo(const QString &host, int port = 37264);
    Q_INVOKABLE void disconnectControl();
    Q_INVOKABLE void setMotion(int yawSpeed, int pitchSpeed);
    Q_INVOKABLE void setZoom(int direction);
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void center();
    Q_INVOKABLE void pointDown();
    Q_INVOKABLE void setAngles(double yaw, double pitch);
    Q_INVOKABLE void setAbsoluteZoom(double multiple);
    Q_INVOKABLE void setMode(int mode);
    Q_INVOKABLE void takePhoto();
    Q_INVOKABLE void toggleRecording();
signals:
    void changed();
private:
    enum class Pending { None, Ack, Mode, Record, Photo };
    bool tcp(quint8 command, const QByteArray &data = {});
    bool udp(quint8 command, const QByteArray &data = {});
    void receive(const A8Protocol::Frame &frame, bool fromTcp);
    void action(quint8 command, const QByteArray &data, const QString &label, Pending kind = Pending::Ack);
    void finish(const QString &message, bool error = false);
    void fail(const QString &message);
    void reset();
    QTcpSocket m_tcp;
    QUdpSocket m_udp;
    QHostAddress m_address;
    quint16 m_port = 37264, m_sequence = 0;
    QByteArray m_buffer;
    QTimer m_continuous, m_telemetry, m_heartbeat, m_timeout, m_connectTimeout;
    QElapsedTimer m_tcpProgress, m_attitudeAge, m_systemAge;
    Pending m_pending = Pending::None;
    quint8 m_pendingCommand = 0;
    int m_target = -1;
    QString m_label, m_status = QStringLiteral("控制未连接"), m_error;
    bool m_connected = false, m_attitudeValid = false, m_failing = false;
    int m_yawSpeed = 0, m_pitchSpeed = 0, m_zoomDirection = 0;
    int m_motionStopRepeats = 0, m_zoomStopRepeats = 0;
    int m_recording = -1, m_mode = -1;
    double m_yaw = 0, m_pitch = 0, m_roll = 0, m_zoom = 0;
};
