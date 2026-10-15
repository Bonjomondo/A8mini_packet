#include "A8GimbalProtocol.h"
#include <QtEndian>
#include <cmath>

namespace A8Protocol {
static void append16(QByteArray &bytes, quint16 value) {
    bytes.append(char(value & 0xff)); bytes.append(char(value >> 8));
}
quint16 crc16(const QByteArray &bytes) {
    quint16 crc = 0;
    for (unsigned char byte : bytes) {
        crc ^= quint16(byte) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
QByteArray encode(quint8 command, const QByteArray &data, quint16 sequence, bool ack) {
    QByteArray bytes = QByteArray::fromHex("5566");
    bytes.append(ack ? '\x01' : '\x00');
    append16(bytes, quint16(data.size())); append16(bytes, sequence);
    bytes.append(char(command)); bytes.append(data);
    append16(bytes, crc16(bytes));
    return bytes;
}
QVector<Frame> takeFrames(QByteArray &buffer) {
    QVector<Frame> frames;
    while (buffer.size() >= 2) {
        int start = buffer.indexOf(QByteArray::fromHex("5566"));
        if (start < 0) { buffer = buffer.endsWith('\x55') ? QByteArray(1, '\x55') : QByteArray(); break; }
        if (start > 0) buffer.remove(0, start);
        if (buffer.size() < 8) break;
        auto length = qFromLittleEndian<quint16>(buffer.constData() + 3);
        if (length > 256 || (quint8(buffer[2]) & 0xfc)) { buffer.remove(0, 1); continue; }
        int size = 10 + length;
        if (buffer.size() < size) break;
        auto bytes = buffer.left(size);
        if (crc16(bytes.left(size - 2)) != qFromLittleEndian<quint16>(bytes.constData() + size - 2)) {
            buffer.remove(0, 1); continue;
        }
        frames.append({quint8(bytes[2]), qFromLittleEndian<quint16>(bytes.constData() + 5),
                       quint8(bytes[7]), bytes.mid(8, length)});
        buffer.remove(0, size);
    }
    return frames;
}
QByteArray angleData(double yaw, double pitch) {
    QByteArray bytes;
    append16(bytes, quint16(qint16(std::lround(yaw * 10))));
    append16(bytes, quint16(qint16(std::lround(pitch * 10))));
    return bytes;
}
qint16 signed16(const QByteArray &bytes, int offset) {
    return qFromLittleEndian<qint16>(bytes.constData() + offset);
}
}
