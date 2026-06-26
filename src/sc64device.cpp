#include "sc64device.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSerialPortInfo>
#include <QThread>
#include <algorithm>
#include <cstring>

namespace {

QByteArray be32(uint32_t value)
{
    QByteArray b(4, 0);
    b[0] = char((value >> 24) & 0xFF);
    b[1] = char((value >> 16) & 0xFF);
    b[2] = char((value >> 8) & 0xFF);
    b[3] = char(value & 0xFF);
    return b;
}

uint32_t read_be32(const char *p)
{
    return (uint32_t(uint8_t(p[0])) << 24) | (uint32_t(uint8_t(p[1])) << 16)
         | (uint32_t(uint8_t(p[2])) << 8) | uint32_t(uint8_t(p[3]));
}

// ROM byte-order normalisation to big-endian z64, matching upload_rom():
//   .v64 (0x37804012) -> swap 2-byte pairs
//   .n64 (0x40123780) -> reverse 4-byte words
//   .z64 (0x80371240) -> no change
void applyEndianSwap(char *d, qint64 n, int mode)
{
    if (mode == 2) {
        for (qint64 i = 0; i + 1 < n; i += 2)
            std::swap(d[i], d[i + 1]);
    } else if (mode == 4) {
        for (qint64 i = 0; i + 3 < n; i += 4) {
            std::swap(d[i], d[i + 3]);
            std::swap(d[i + 1], d[i + 2]);
        }
    }
}

} // namespace

QList<Sc64Device::PortInfo> Sc64Device::listDevices()
{
    QList<PortInfo> out;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (info.hasVendorIdentifier() && info.hasProductIdentifier()
            && info.vendorIdentifier() == SC64_VID
            && info.productIdentifier() == SC64_PID) {
            out.push_back({ info.portName(), info.serialNumber(), info.description() });
        }
    }
    std::sort(out.begin(), out.end(),
              [](const PortInfo &a, const PortInfo &b) { return a.serialNumber < b.serialNumber; });
    return out;
}

bool Sc64Device::open(const QString &portName, QString *error)
{
    if (m_port.isOpen())
        m_port.close();

    m_port.setPortName(portName);
    m_port.setBaudRate(BAUD);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        if (error)
            *error = m_port.errorString();
        return false;
    }
    return true;
}

void Sc64Device::close()
{
    if (m_port.isOpen())
        m_port.close();
}

bool Sc64Device::reset(QString *error)
{
    m_port.clear(QSerialPort::Output);

    QElapsedTimer timer;
    timer.start();
    m_port.setDataTerminalReady(true);
    while (!(m_port.pinoutSignals() & QSerialPort::DataSetReadySignal)) {
        if (timer.elapsed() > RESET_TIMEOUT_MS) {
            if (error)
                *error = QStringLiteral("Couldn't reset SC64 device (on)");
            return false;
        }
        QThread::msleep(1);
    }

    m_port.clear(QSerialPort::Input);

    timer.restart();
    m_port.setDataTerminalReady(false);
    while (m_port.pinoutSignals() & QSerialPort::DataSetReadySignal) {
        if (timer.elapsed() > RESET_TIMEOUT_MS) {
            if (error)
                *error = QStringLiteral("Couldn't reset SC64 device (off)");
            return false;
        }
        QThread::msleep(1);
    }
    return true;
}

bool Sc64Device::writeAll(const QByteArray &data, QString *error)
{
    qint64 written = 0;
    while (written < data.size()) {
        qint64 n = m_port.write(data.constData() + written, data.size() - written);
        if (n < 0) {
            if (error)
                *error = m_port.errorString();
            return false;
        }
        written += n;
        if (!m_port.waitForBytesWritten(IO_TIMEOUT_MS)) {
            if (error)
                *error = QStringLiteral("Write timeout");
            return false;
        }
    }
    return true;
}

bool Sc64Device::readExact(char *buffer, qint64 length, QString *error)
{
    qint64 got = 0;
    while (got < length) {
        if (m_port.bytesAvailable() == 0) {
            if (!m_port.waitForReadyRead(IO_TIMEOUT_MS)) {
                if (error)
                    *error = QStringLiteral("Read timeout");
                return false;
            }
        }
        qint64 n = m_port.read(buffer + got, length - got);
        if (n < 0) {
            if (error)
                *error = m_port.errorString();
            return false;
        }
        got += n;
    }
    return true;
}

bool Sc64Device::executeCommand(char id, uint32_t arg0, uint32_t arg1,
                                const QByteArray &data, QByteArray *response,
                                bool *deviceError, QString *error)
{
    QByteArray frame;
    frame.append("CMD", 3);
    frame.append(id);
    frame.append(be32(arg0));
    frame.append(be32(arg1));
    frame.append(data);

    if (!writeAll(frame, error))
        return false;

    for (;;) {
        char header[4];
        if (!readExact(header, 4, error))
            return false;

        bool isPacket = false;
        bool isError = false;
        if (std::memcmp(header, "CMP", 3) == 0) {
            // complete
        } else if (std::memcmp(header, "PKT", 3) == 0) {
            isPacket = true;
        } else if (std::memcmp(header, "ERR", 3) == 0) {
            isError = true;
        } else {
            if (error)
                *error = QStringLiteral("Unexpected response token");
            return false;
        }

        char lengthBytes[4];
        if (!readExact(lengthBytes, 4, error))
            return false;
        uint32_t length = read_be32(lengthBytes);

        QByteArray payload(int(length), Qt::Uninitialized);
        if (length > 0 && !readExact(payload.data(), length, error))
            return false;

        if (isPacket)
            continue; // async packet; ignored in this scaffold

        if (response)
            *response = payload;
        if (deviceError)
            *deviceError = isError;
        return true;
    }
}

bool Sc64Device::identify(QByteArray *idOut, QString *error)
{
    QByteArray resp;
    bool deviceError = false;
    if (!executeCommand('v', 0, 0, QByteArray(), &resp, &deviceError, error))
        return false;
    if (deviceError) {
        if (error)
            *error = QStringLiteral("Device returned an error on identify");
        return false;
    }
    if (idOut)
        *idOut = resp;
    return true;
}

bool Sc64Device::uploadRom(const QString &filePath, ProgressFn progress, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    const qint64 total = file.size();
    if (total > MAX_ROM_LENGTH) {
        if (error)
            *error = QStringLiteral("ROM is larger than the 78 MiB maximum");
        return false;
    }
    if (total > SDRAM_LENGTH) {
        if (error)
            *error = QStringLiteral(
                "ROMs larger than 64 MiB need the flash shadow/extended path "
                "(not implemented in this scaffold yet)");
        return false;
    }

    // Detect byte order from the first 4 bytes.
    int swapMode = 0;
    QByteArray head = file.peek(4);
    if (head.size() == 4) {
        const uint8_t b0 = head[0], b1 = head[1], b2 = head[2], b3 = head[3];
        if (b0 == 0x37 && b1 == 0x80 && b2 == 0x40 && b3 == 0x12)
            swapMode = 2; // .v64
        else if (b0 == 0x40 && b1 == 0x12 && b2 == 0x37 && b3 == 0x80)
            swapMode = 4; // .n64
    }

    uint32_t address = SDRAM_ADDRESS;
    qint64 done = 0;
    QByteArray chunk(int(MEMORY_CHUNK_LENGTH), Qt::Uninitialized);

    if (progress)
        progress(0, total);

    for (;;) {
        qint64 n = file.read(chunk.data(), MEMORY_CHUNK_LENGTH);
        if (n < 0) {
            if (error)
                *error = file.errorString();
            return false;
        }
        if (n == 0)
            break;

        applyEndianSwap(chunk.data(), n, swapMode);

        QByteArray slice = QByteArray::fromRawData(chunk.constData(), int(n));
        bool deviceError = false;
        if (!executeCommand('M', address, uint32_t(n), slice, nullptr, &deviceError, error))
            return false;
        if (deviceError) {
            if (error)
                *error = QStringLiteral("Device error during memory write");
            return false;
        }

        address += uint32_t(n);
        done += n;
        if (progress)
            progress(done, total);
    }

    return true;
}

QString Sc64Device::sdResultMessage(uint32_t code)
{
    switch (code) {
    case 0:  return QStringLiteral("OK");
    case 1:  return QStringLiteral("No card in slot");
    case 2:  return QStringLiteral("Not initialized");
    case 3:  return QStringLiteral("Invalid argument");
    case 4:  return QStringLiteral("Invalid address");
    case 5:  return QStringLiteral("Invalid operation");
    default: return QStringLiteral("SD error code %1").arg(code);
    }
}

bool Sc64Device::sdCardOp(uint32_t address, uint32_t op, uint32_t *result,
                          uint32_t *status, QString *error)
{
    QByteArray resp;
    bool deviceError = false;
    if (!executeCommand('i', address, op, QByteArray(), &resp, &deviceError, error))
        return false;
    if (deviceError) {
        if (error)
            *error = QStringLiteral("Device error during SD card operation");
        return false;
    }
    if (resp.size() < 8) {
        if (error)
            *error = QStringLiteral("Short SD card operation response");
        return false;
    }
    if (result)
        *result = read_be32(resp.constData());
    if (status)
        *status = read_be32(resp.constData() + 4);
    return true;
}

bool Sc64Device::sdInit(QString *error)
{
    uint32_t result = 0, status = 0;
    if (!sdCardOp(0, 1 /* Init */, &result, &status, error))
        return false;
    if (result != 0) {
        if (error)
            *error = QStringLiteral("SD init failed: ") + sdResultMessage(result);
        return false;
    }
    return true;
}

bool Sc64Device::readMemory(uint32_t address, uint32_t length, char *out, QString *error)
{
    QByteArray resp;
    bool deviceError = false;
    if (!executeCommand('m', address, length, QByteArray(), &resp, &deviceError, error))
        return false;
    if (deviceError) {
        if (error)
            *error = QStringLiteral("Device error during memory read");
        return false;
    }
    if (resp.size() != int(length)) {
        if (error)
            *error = QStringLiteral("Short memory-read response");
        return false;
    }
    std::memcpy(out, resp.constData(), length);
    return true;
}

bool Sc64Device::readSectors(uint32_t startSector, uint32_t count, char *out, QString *error)
{
    uint32_t sector = startSector;
    qint64 offset = 0;
    qint64 remaining = qint64(count) * SD_CARD_SECTOR_SIZE;

    while (remaining > 0) {
        const qint64 want = qMin<qint64>(SD_CARD_BUFFER_LENGTH, remaining);
        const uint32_t sectors = uint32_t(want / SD_CARD_SECTOR_SIZE);

        // 's': read `sectors` from the card into the SDRAM buffer
        QByteArray resp;
        bool deviceError = false;
        if (!executeCommand('s', SD_CARD_BUFFER_ADDRESS, sectors, be32(sector),
                            &resp, &deviceError, error))
            return false;
        if (deviceError) {
            if (error)
                *error = QStringLiteral("Device error during SD read");
            return false;
        }
        if (resp.size() >= 4) {
            const uint32_t result = read_be32(resp.constData());
            if (result != 0) {
                if (error)
                    *error = QStringLiteral("SD read failed: ") + sdResultMessage(result);
                return false;
            }
        }

        // 'm': copy the buffer back to the host
        if (!readMemory(SD_CARD_BUFFER_ADDRESS, uint32_t(want), out + offset, error))
            return false;

        sector += sectors;
        offset += want;
        remaining -= want;
    }
    return true;
}

bool Sc64Device::writeSectors(uint32_t startSector, uint32_t count, const char *in, QString *error)
{
    uint32_t sector = startSector;
    qint64 offset = 0;
    qint64 remaining = qint64(count) * SD_CARD_SECTOR_SIZE;

    while (remaining > 0) {
        const qint64 want = qMin<qint64>(SD_CARD_BUFFER_LENGTH, remaining);
        const uint32_t sectors = uint32_t(want / SD_CARD_SECTOR_SIZE);

        // 'M': stage host data into the SDRAM buffer
        QByteArray slice = QByteArray::fromRawData(in + offset, int(want));
        bool deviceError = false;
        if (!executeCommand('M', SD_CARD_BUFFER_ADDRESS, uint32_t(want), slice,
                            nullptr, &deviceError, error))
            return false;
        if (deviceError) {
            if (error)
                *error = QStringLiteral("Device error staging SD data");
            return false;
        }

        // 'S': flush the buffer to the card
        QByteArray resp;
        deviceError = false;
        if (!executeCommand('S', SD_CARD_BUFFER_ADDRESS, sectors, be32(sector),
                            &resp, &deviceError, error))
            return false;
        if (deviceError) {
            if (error)
                *error = QStringLiteral("Device error during SD write");
            return false;
        }
        if (resp.size() >= 4) {
            const uint32_t result = read_be32(resp.constData());
            if (result != 0) {
                if (error)
                    *error = QStringLiteral("SD write failed: ") + sdResultMessage(result);
                return false;
            }
        }

        sector += sectors;
        offset += want;
        remaining -= want;
    }
    return true;
}
