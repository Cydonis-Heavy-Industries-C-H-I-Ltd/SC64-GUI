#include "sc64filesystem.h"

#include <QFile>
#include <QByteArray>

extern "C" {
#include "ff.h"
}

// Set the active device for the FatFs disk glue (defined in fatfs_glue.cpp).
void sc64_fatfs_set_device(Sc64Device *device);

namespace {

QString frToString(FRESULT fr)
{
    switch (fr) {
    case FR_OK:                  return QStringLiteral("OK");
    case FR_DISK_ERR:            return QStringLiteral("Low-level disk I/O error");
    case FR_INT_ERR:             return QStringLiteral("Internal FatFs error");
    case FR_NOT_READY:           return QStringLiteral("Drive not ready");
    case FR_NO_FILE:             return QStringLiteral("File not found");
    case FR_NO_PATH:             return QStringLiteral("Path not found");
    case FR_INVALID_NAME:        return QStringLiteral("Invalid path name");
    case FR_DENIED:              return QStringLiteral("Access denied or directory full");
    case FR_EXIST:               return QStringLiteral("File already exists");
    case FR_INVALID_OBJECT:      return QStringLiteral("Invalid file/directory object");
    case FR_WRITE_PROTECTED:     return QStringLiteral("Write protected");
    case FR_INVALID_DRIVE:       return QStringLiteral("Invalid drive");
    case FR_NOT_ENABLED:         return QStringLiteral("Volume has no work area");
    case FR_NO_FILESYSTEM:       return QStringLiteral("No valid FAT/exFAT volume found");
    case FR_TIMEOUT:             return QStringLiteral("Timed out");
    case FR_LOCKED:              return QStringLiteral("Locked by the file-sharing policy");
    case FR_NOT_ENOUGH_CORE:     return QStringLiteral("Out of memory (LFN buffer)");
    case FR_TOO_MANY_OPEN_FILES: return QStringLiteral("Too many open files");
    case FR_INVALID_PARAMETER:   return QStringLiteral("Invalid parameter");
    default:                     return QStringLiteral("FatFs error %1").arg(int(fr));
    }
}

} // namespace

Sc64FileSystem::~Sc64FileSystem()
{
    unmount();
}

bool Sc64FileSystem::mount(QString *error)
{
    if (m_mounted)
        return true;

    sc64_fatfs_set_device(m_device);

    auto *fs = new FATFS();
    // Third arg 1 = mount immediately (so errors surface now, not on first access).
    FRESULT fr = f_mount(fs, "", 1);
    if (fr != FR_OK) {
        delete fs;
        if (error)
            *error = frToString(fr);
        return false;
    }
    m_fs = fs;
    m_mounted = true;
    return true;
}

void Sc64FileSystem::unmount()
{
    if (m_mounted) {
        sc64_fatfs_set_device(m_device);
        f_mount(nullptr, "", 0);
        m_mounted = false;
    }
    if (m_fs) {
        delete static_cast<FATFS *>(m_fs);
        m_fs = nullptr;
    }
}

bool Sc64FileSystem::list(const QString &path, QList<Entry> *out, QString *error)
{
    if (!m_mounted && !mount(error))
        return false;

    sc64_fatfs_set_device(m_device);

    DIR dir;
    const QByteArray p = path.isEmpty() ? QByteArray("/") : path.toUtf8();
    FRESULT fr = f_opendir(&dir, p.constData());
    if (fr != FR_OK) {
        if (error)
            *error = frToString(fr);
        return false;
    }

    for (;;) {
        FILINFO fno;
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) {
            f_closedir(&dir);
            if (error)
                *error = frToString(fr);
            return false;
        }
        if (fno.fname[0] == 0)
            break; // end of directory

        Entry e;
        e.name = QString::fromUtf8(fno.fname);
        e.isDir = (fno.fattrib & AM_DIR) != 0;
        e.size = quint64(fno.fsize);
        out->append(e);
    }

    f_closedir(&dir);
    return true;
}

bool Sc64FileSystem::copyToCard(const QString &hostPath, const QString &cardPath,
                                Sc64Device::ProgressFn progress, QString *error)
{
    if (!m_mounted && !mount(error))
        return false;

    sc64_fatfs_set_device(m_device);

    QFile in(hostPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = in.errorString();
        return false;
    }
    const qint64 total = in.size();

    FIL fil;
    const QByteArray cp = cardPath.toUtf8();
    FRESULT fr = f_open(&fil, cp.constData(), FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        if (error)
            *error = frToString(fr);
        return false;
    }

    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    qint64 done = 0;
    bool ok = true;
    if (progress)
        progress(0, total);

    for (;;) {
        qint64 n = in.read(buffer.data(), buffer.size());
        if (n < 0) {
            if (error)
                *error = in.errorString();
            ok = false;
            break;
        }
        if (n == 0)
            break;

        UINT written = 0;
        fr = f_write(&fil, buffer.constData(), UINT(n), &written);
        if (fr != FR_OK || written != UINT(n)) {
            if (error)
                *error = (fr != FR_OK) ? frToString(fr)
                                       : QStringLiteral("Short write (card full?)");
            ok = false;
            break;
        }
        done += n;
        if (progress)
            progress(done, total);
    }

    f_close(&fil);
    return ok;
}
