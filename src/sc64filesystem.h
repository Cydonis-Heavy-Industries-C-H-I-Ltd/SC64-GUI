#pragma once

#include "sc64device.h"

#include <QList>
#include <QString>
#include <functional>

// Filesystem-aware access to the SD card via FatFs. Mounts the existing FAT/
// exFAT volume and copies files into it without disturbing other data (FatFs
// only rewrites the FAT, the directory entry, and the new file's clusters).
class Sc64FileSystem
{
public:
    struct Entry {
        QString name;
        bool isDir = false;
        quint64 size = 0;
    };

    explicit Sc64FileSystem(Sc64Device *device) : m_device(device) {}
    ~Sc64FileSystem();

    bool mount(QString *error);
    void unmount();
    bool isMounted() const { return m_mounted; }

    bool list(const QString &path, QList<Entry> *out, QString *error);

    bool mkdir(const QString &path, QString *error);
    bool rename(const QString &oldPath, const QString &newPath, QString *error);
    bool remove(const QString &path, QString *error); // recursive for directories

    // Copy a host file into the card at `cardPath` (e.g. "/roms/game.z64").
    bool copyToCard(const QString &hostPath, const QString &cardPath,
                    Sc64Device::ProgressFn progress, QString *error);

private:
    Sc64Device *m_device = nullptr;
    void *m_fs = nullptr; // FATFS*, kept opaque to avoid leaking ff.h
    bool m_mounted = false;
};
