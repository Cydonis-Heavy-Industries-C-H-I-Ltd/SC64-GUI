#include "sc64controller.h"
#include "sc64filesystem.h"

#include <QFileInfo>
#include <QVariantMap>

// ---------------- Sc64Worker (runs in the worker thread) ----------------

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
    if (idStr != QStringLiteral("SC64")) {
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

void Sc64Worker::listCard(const QString &path)
{
    if (!m_device || !m_device->isOpen()) {
        emit cardListed({}, QStringLiteral("Not connected"));
        return;
    }
    if (!m_fs)
        m_fs = new Sc64FileSystem(m_device);

    QString err;
    QList<Sc64FileSystem::Entry> entries;
    if (!m_fs->list(path, &entries, &err)) {
        emit cardListed({}, QStringLiteral("Card read failed: ") + err);
        return;
    }

    QVariantList out;
    for (const auto &e : entries) {
        QVariantMap m;
        m["name"] = e.name;
        m["isDir"] = e.isDir;
        m["size"] = qulonglong(e.size);
        out.append(m);
    }
    emit cardListed(out, QStringLiteral("Card: %1 item(s)").arg(out.size()));
}

void Sc64Worker::copyToCard(const QString &hostPath, const QString &cardPath)
{
    if (!m_device || !m_device->isOpen()) {
        emit uploadFinished(false, QStringLiteral("Not connected"));
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
    emit uploadFinished(ok, ok ? QStringLiteral("Copied to card: ") + cardPath
                                : QStringLiteral("Copy failed: ") + err);
    if (ok)
        listCard(QStringLiteral("/"));
}

// ---------------- Sc64Controller (runs in the GUI thread) ----------------

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
    connect(m_worker, &Sc64Worker::cardListed, this,
            [this](const QVariantList &entries, const QString &msg) {
                m_cardEntries = entries;
                emit cardEntriesChanged();
                setBusy(false);
                setStatus(msg);
            });

    m_thread.start();
    emit requestRefresh();
}

Sc64Controller::~Sc64Controller()
{
    m_thread.quit();
    m_thread.wait();
}

void Sc64Controller::refresh()
{
    emit requestRefresh();
}

void Sc64Controller::connectPort(const QString &port)
{
    setStatus(QStringLiteral("Connecting to %1...").arg(port));
    emit requestConnect(port);
}

void Sc64Controller::disconnectPort()
{
    emit requestDisconnect();
}

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
    emit requestListCard(QStringLiteral("/"));
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
    const QString cardPath = QStringLiteral("/") + QFileInfo(path).fileName();
    setBusy(true);
    setProgress(0.0);
    setStatus(QStringLiteral("Copying %1 to card...").arg(QFileInfo(path).fileName()));
    emit requestCopyToCard(path, cardPath);
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
