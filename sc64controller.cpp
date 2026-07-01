#include "sc64controller.h"
#include "sc64filesystem.h"

#include <QFileInfo>
#include <QVariantMap>

namespace {

QVariantList toVariant(const QList<Sc64FileSystem::Entry> &entries)
{
    QVariantList out;
    for (const auto &e : entries) {
        QVariantMap m;
        m["name"] = e.name;
        m["isDir"] = e.isDir;
        m["size"] = qulonglong(e.size);
        out.append(m);
    }
    return out;
}

QString joinPath(const QString &dir, const QString &name)
{
    if (dir.isEmpty() || dir == QStringLiteral("/"))
        return QStringLiteral("/") + name;
    return dir + QStringLiteral("/") + name;
}

QString parentPath(const QString &dir)
{
    if (dir == QStringLiteral("/") || dir.isEmpty())
        return QStringLiteral("/");
    const int i = dir.lastIndexOf('/');
    return (i <= 0) ? QStringLiteral("/") : dir.left(i);
}

} // namespace

// ---------------- Sc64Worker ----------------

Sc64Worker::~Sc64Worker()
{
    delete m_fs;
    delete m_device;
}

void Sc64Worker::refresh()
{
    if (!m_device)
        m_device = new Sc64Device();
    QStringList ports;
    for (const auto &d : Sc64Device::listDevices())
        ports << d.portName;
    emit devicesFound(ports);
}

void Sc64Worker::connectPort(const QString &port)
{
    if (!m_device)
        m_device = new Sc64Device();
    QString err;
    if (!m_device->open(port, &err)) {
        emit connectionChanged(false, QStringLiteral("Open failed: ") + err);
        return;
    }
    if (!m_device->reset(&err)) {
        m_device->close();
        emit connectionChanged(false, QStringLiteral("Reset failed: ") + err);
        return;
    }
    QByteArray id;
    if (!m_device->identify(&id, &err)) {
        m_device->close();
        emit connectionChanged(false, QStringLiteral("Identify failed: ") + err);
        return;
    }
    const QString idStr = QString::fromLatin1(id);
    if (idStr != QStringLiteral("SCv2")) {
        m_device->close();
        emit connectionChanged(false, QStringLiteral("Unexpected identifier: ") + idStr);
        return;
    }
    emit connectionChanged(true, QStringLiteral("Connected to %1 (%2)").arg(port, idStr));
}

void Sc64Worker::disconnectPort()
{
    if (m_fs) {
        delete m_fs; // unmounts
        m_fs = nullptr;
    }
    if (m_device)
        m_device->close();
    emit connectionChanged(false, QStringLiteral("Disconnected"));
}

void Sc64Worker::uploadRom(const QString &path)
{
    if (!m_device || !m_device->isOpen()) {
        emit uploadFinished(false, QStringLiteral("Not connected"));
        return;
    }
    QString err;
    const bool ok = m_device->uploadRom(
        path,
        [this](qint64 done, qint64 total) {
            if (total > 0)
                emit progress(double(done) / double(total));
        },
        &err);
    emit uploadFinished(ok, ok ? QStringLiteral("Upload complete")
                                : QStringLiteral("Upload failed: ") + err);
}

void Sc64Worker::relist(const QString &path, const QString &message)
{
    if (!m_fs)
        m_fs = new Sc64FileSystem(m_device);
    QList<Sc64FileSystem::Entry> entries;
    QString err;
    if (!m_fs->list(path, &entries, &err)) {
        emit cardUpdated({}, path,
                         message.isEmpty() ? QStringLiteral("Card read failed: ") + err : message);
        return;
    }
    emit cardUpdated(toVariant(entries), path, message);
}

void Sc64Worker::listCard(const QString &path)
{
    if (!m_device || !m_device->isOpen()) {
        emit cardUpdated({}, path, QStringLiteral("Not connected"));
        return;
    }
    relist(path, QString());
}

void Sc64Worker::makeDir(const QString &absPath, const QString &refreshPath)
{
    if (!m_device || !m_device->isOpen()) {
        emit cardUpdated({}, refreshPath, QStringLiteral("Not connected"));
        return;
    }
    if (!m_fs)
        m_fs = new Sc64FileSystem(m_device);
    QString err;
    if (!m_fs->mkdir(absPath, &err)) {
        relist(refreshPath, QStringLiteral("Create folder failed: ") + err);
        return;
    }
    relist(refreshPath, QStringLiteral("Folder created"));
}

void Sc64Worker::renameEntry(const QString &absOld, const QString &absNew, const QString &refreshPath)
{
    if (!m_device || !m_device->isOpen()) {
        emit cardUpdated({}, refreshPath, QStringLiteral("Not connected"));
        return;
    }
    if (!m_fs)
        m_fs = new Sc64FileSystem(m_device);
    QString err;
    if (!m_fs->rename(absOld, absNew, &err)) {
        relist(refreshPath, QStringLiteral("Rename failed: ") + err);
        return;
    }
    relist(refreshPath, QStringLiteral("Renamed"));
}

void Sc64Worker::deleteEntry(const QString &absPath, const QString &refreshPath)
{
    if (!m_device || !m_device->isOpen()) {
        emit cardUpdated({}, refreshPath, QStringLiteral("Not connected"));
        return;
    }
    if (!m_fs)
        m_fs = new Sc64FileSystem(m_device);
    QString err;
    if (!m_fs->remove(absPath, &err)) {
        relist(refreshPath, QStringLiteral("Delete failed: ") + err);
        return;
    }
    relist(refreshPath, QStringLiteral("Deleted"));
}

void Sc64Worker::copyToCard(const QString &hostPath, const QString &cardPath, const QString &refreshPath)
{
    if (!m_device || !m_device->isOpen()) {
        emit cardUpdated({}, refreshPath, QStringLiteral("Not connected"));
        return;
    }
    if (!m_fs)
        m_fs = new Sc64FileSystem(m_device);
    QString err;
    const bool ok = m_fs->copyToCard(
        hostPath, cardPath,
        [this](qint64 done, qint64 total) {
            if (total > 0)
                emit progress(double(done) / double(total));
        },
        &err);
    relist(refreshPath, ok ? QStringLiteral("Copied to card")
                           : QStringLiteral("Copy failed: ") + err);
}

// ---------------- Sc64Controller ----------------

Sc64Controller::Sc64Controller(QObject *parent) : QObject(parent)
{
    m_worker = new Sc64Worker();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this, &Sc64Controller::requestRefresh, m_worker, &Sc64Worker::refresh);
    connect(this, &Sc64Controller::requestConnect, m_worker, &Sc64Worker::connectPort);
    connect(this, &Sc64Controller::requestDisconnect, m_worker, &Sc64Worker::disconnectPort);
    connect(this, &Sc64Controller::requestUpload, m_worker, &Sc64Worker::uploadRom);
    connect(this, &Sc64Controller::requestListCard, m_worker, &Sc64Worker::listCard);
    connect(this, &Sc64Controller::requestMakeDir, m_worker, &Sc64Worker::makeDir);
    connect(this, &Sc64Controller::requestRename, m_worker, &Sc64Worker::renameEntry);
    connect(this, &Sc64Controller::requestDelete, m_worker, &Sc64Worker::deleteEntry);
    connect(this, &Sc64Controller::requestCopyToCard, m_worker, &Sc64Worker::copyToCard);

    connect(m_worker, &Sc64Worker::devicesFound, this, [this](const QStringList &d) {
        m_devices = d;
        emit devicesChanged();
        setStatus(d.isEmpty() ? QStringLiteral("No SC64 devices found")
                              : QStringLiteral("Found %1 device(s)").arg(d.size()));
    });
    connect(m_worker, &Sc64Worker::connectionChanged, this,
            [this](bool c, const QString &info) {
                m_connected = c;
                emit connectedChanged();
                if (!c) {
                    m_cardEntries.clear();
                    emit cardEntriesChanged();
                    m_currentPath = QStringLiteral("/");
                    emit currentPathChanged();
                }
                setStatus(info);
            });
    connect(m_worker, &Sc64Worker::progress, this, [this](double f) { setProgress(f); });
    connect(m_worker, &Sc64Worker::uploadFinished, this,
            [this](bool ok, const QString &msg) {
                setBusy(false);
                if (ok)
                    setProgress(1.0);
                setStatus(msg);
            });
    connect(m_worker, &Sc64Worker::cardUpdated, this,
            [this](const QVariantList &entries, const QString &path, const QString &message) {
                m_cardEntries = entries;
                emit cardEntriesChanged();
                if (m_currentPath != path) {
                    m_currentPath = path;
                    emit currentPathChanged();
                }
                setBusy(false);
                setStatus(message.isEmpty()
                              ? QStringLiteral("%1 — %2 item(s)").arg(path).arg(entries.size())
                              : message);
            });

    m_thread.start();
    emit requestRefresh();
}

Sc64Controller::~Sc64Controller()
{
    m_thread.quit();
    m_thread.wait();
}

void Sc64Controller::refresh() { emit requestRefresh(); }

void Sc64Controller::connectPort(const QString &port)
{
    setStatus(QStringLiteral("Connecting to %1...").arg(port));
    emit requestConnect(port);
}

void Sc64Controller::disconnectPort() { emit requestDisconnect(); }

void Sc64Controller::uploadRom(const QUrl &fileUrl)
{
    if (m_busy)
        return;
    const QString path = fileUrl.toLocalFile();
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Invalid file"));
        return;
    }
    setBusy(true);
    setProgress(0.0);
    setStatus(QStringLiteral("Uploading %1...").arg(path));
    emit requestUpload(path);
}

void Sc64Controller::refreshCard()
{
    if (m_busy)
        return;
    setBusy(true);
    setStatus(QStringLiteral("Reading card..."));
    emit requestListCard(m_currentPath);
}

void Sc64Controller::openDir(const QString &name)
{
    if (m_busy)
        return;
    setBusy(true);
    emit requestListCard(joinPath(m_currentPath, name));
}

void Sc64Controller::navigateUp()
{
    if (m_busy || atRoot())
        return;
    setBusy(true);
    emit requestListCard(parentPath(m_currentPath));
}

void Sc64Controller::makeDir(const QString &name)
{
    if (m_busy || name.trimmed().isEmpty())
        return;
    setBusy(true);
    emit requestMakeDir(joinPath(m_currentPath, name.trimmed()), m_currentPath);
}

void Sc64Controller::renameEntry(const QString &oldName, const QString &newName)
{
    if (m_busy || newName.trimmed().isEmpty() || oldName == newName.trimmed())
        return;
    setBusy(true);
    emit requestRename(joinPath(m_currentPath, oldName),
                       joinPath(m_currentPath, newName.trimmed()), m_currentPath);
}

void Sc64Controller::deleteEntry(const QString &name)
{
    if (m_busy)
        return;
    setBusy(true);
    emit requestDelete(joinPath(m_currentPath, name), m_currentPath);
}

void Sc64Controller::copyToCard(const QUrl &fileUrl)
{
    if (m_busy)
        return;
    const QString path = fileUrl.toLocalFile();
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Invalid file"));
        return;
    }
    const QString cardPath = joinPath(m_currentPath, QFileInfo(path).fileName());
    setBusy(true);
    setProgress(0.0);
    setStatus(QStringLiteral("Copying %1...").arg(QFileInfo(path).fileName()));
    emit requestCopyToCard(path, cardPath, m_currentPath);
}

void Sc64Controller::setBusy(bool b)
{
    if (m_busy != b) {
        m_busy = b;
        emit busyChanged();
    }
}

void Sc64Controller::setProgress(double p)
{
    if (!qFuzzyCompare(m_progress, p)) {
        m_progress = p;
        emit progressChanged();
    }
}

void Sc64Controller::setStatus(const QString &s)
{
    if (m_status != s) {
        m_status = s;
        emit statusChanged();
    }
}
