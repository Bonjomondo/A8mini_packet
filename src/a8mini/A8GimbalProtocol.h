#pragma once
#include <QByteArray>
#include <QVector>

namespace A8Protocol {
struct Frame {
    quint8 control;
    quint16 sequence;
    quint8 command;
    QByteArray data;
};
quint16 crc16(const QByteArray &bytes);
QByteArray encode(quint8 command, const QByteArray &data, quint16 sequence, bool ack = true);
// Keeps incomplete TCP frames and resynchronizes after corrupt data.
QVector<Frame> takeFrames(QByteArray &buffer);
QByteArray angleData(double yaw, double pitch);
qint16 signed16(const QByteArray &bytes, int offset);
}
