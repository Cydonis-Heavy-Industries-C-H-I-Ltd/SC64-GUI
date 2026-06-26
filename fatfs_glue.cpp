// Glue between FatFs and the SummerCart64. FatFs (compiled as C) calls these
// disk_* functions; we route them to the SC64 raw sector commands. Single drive
// (pdrv 0). All access happens on the worker thread, so the device pointer is a
// simple file-scope variable.

#include "sc64device.h"

#include <QDateTime>
#include <QString>

extern "C" {
#include "ff.h"
#include "diskio.h"
}

namespace {
Sc64Device *s_device = nullptr;
}

void sc64_fatfs_set_device(Sc64Device *device)
{
    s_device = device;
}

extern "C" DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NOINIT;
    return (s_device && s_device->isOpen()) ? 0 : STA_NOINIT;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0 || !s_device)
        return STA_NOINIT;
    QString error;
    return s_device->sdInit(&error) ? 0 : STA_NOINIT;
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !s_device)
        return RES_PARERR;
    QString error;
    return s_device->readSectors(uint32_t(sector), uint32_t(count),
                                 reinterpret_cast<char *>(buff), &error)
               ? RES_OK
               : RES_ERROR;
}

extern "C" DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !s_device)
        return RES_PARERR;
    QString error;
    return s_device->writeSectors(uint32_t(sector), uint32_t(count),
                                  reinterpret_cast<const char *>(buff), &error)
               ? RES_OK
               : RES_ERROR;
}

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)
        return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_SIZE:
        *static_cast<WORD *>(buff) = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *static_cast<DWORD *>(buff) = 1;
        return RES_OK;
    case GET_SECTOR_COUNT:
        // Only needed for f_mkfs (formatting), which we don't do.
        return RES_ERROR;
    default:
        return RES_OK;
    }
}

// FatFs timestamp for created/modified files, from the host clock.
extern "C" DWORD get_fattime(void)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    if (d.year() < 1980)
        return 0;
    return (DWORD(d.year() - 1980) << 25) | (DWORD(d.month()) << 21)
         | (DWORD(d.day()) << 16) | (DWORD(t.hour()) << 11)
         | (DWORD(t.minute()) << 5) | (DWORD(t.second() / 2));
}
