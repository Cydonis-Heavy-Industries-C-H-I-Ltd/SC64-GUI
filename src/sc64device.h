#pragma once

#include <QByteArray>
#include <QList>
#include <QSerialPort>
#include <QString>
#include <functional>
#include <cstdint>

// Implements the SummerCart64 USB serial protocol (Option A: pure Qt, no Rust).
// Frame format (matches sw/deployer/src/sc64): command = "CMD" + id(1) +
// arg0(u32 BE) + arg1(u32 BE) + payload; response = ("CMP"|"PKT"|"ERR") +
// id(1) + length(u32 BE) + data.
class Sc64Device
{
public:
    struct PortInfo {
        QString portName;
        QString serialNumber;
        QString description;
    };

    using ProgressFn = std::function<void(qint64 done, qint64 total)>;

    // Enumerate SC64 devices by USB VID/PID (FTDI 0x0403:0x6014).
    static QList<PortInfo> listDevices();

    bool open(const QString &portName, QString *error);
    void close();
    bool isOpen() const { return m_port.isOpen(); }

    // Hardware reset via the DTR/DSR handshake the firmware expects.
    bool reset(QString *error);

    // 'v' command: returns the 4-byte identifier (expected "SC64").
    bool identify(QByteArray *idOut, QString *error);

    // Send one command, collect the matching response (skips async PKTs).
    bool executeCommand(char id, uint32_t arg0, uint32_t arg1,
                        const QByteArray &data, QByteArray *response,
                        bool *deviceError, QString *error);

    // Stream a ROM into SDRAM at 0x0 with endian auto-detection + progress.
    bool uploadRom(const QString &filePath, ProgressFn progress, QString *error);

    // Initialise the SD card ('i' op = Init). Required before read/write.
    bool sdInit(QString *error);

    // Raw sector primitives, used by the FatFs filesystem layer.
    // `count` sectors of 512 bytes each, starting at `startSector`.
    bool readSectors(uint32_t startSector, uint32_t count, char *out, QString *error);
    bool writeSectors(uint32_t startSector, uint32_t count, const char *in, QString *error);

private:
    bool readMemory(uint32_t address, uint32_t length, char *out, QString *error);
    bool sdCardOp(uint32_t address, uint32_t op, uint32_t *result,
                  uint32_t *status, QString *error);
    static QString sdResultMessage(uint32_t code);

    bool writeAll(const QByteArray &data, QString *error);
    bool readExact(char *buffer, qint64 length, QString *error);

    QSerialPort m_port;

    static constexpr quint16 SC64_VID = 0x0403;
    static constexpr quint16 SC64_PID = 0x6014;
    static constexpr int     BAUD     = 115200;          // nominal; FT232H runs at USB speed

    static constexpr uint32_t SDRAM_ADDRESS      = 0x0000'0000;
    static constexpr qint64   SDRAM_LENGTH       = 64ll * 1024 * 1024;
    static constexpr qint64   MAX_ROM_LENGTH     = 78ll * 1024 * 1024;
    static constexpr qint64   MEMORY_CHUNK_LENGTH = 1ll * 1024 * 1024;
    static constexpr int      IO_TIMEOUT_MS      = 5000;
    static constexpr int      RESET_TIMEOUT_MS   = 1000;

    static constexpr int      SD_CARD_SECTOR_SIZE    = 512;
    static constexpr uint32_t SD_CARD_BUFFER_ADDRESS = 0x03FE'0000;
    static constexpr qint64   SD_CARD_BUFFER_LENGTH  = 128 * 1024;
};
