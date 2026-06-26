#pragma once

#include "sc64device.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QVariantList>

class Sc64FileSystem;

// Lives in a worker thread and owns the Sc64Device + filesystem, so all blocking
// serial I/O (and the QSerialPort itself) stays off the GUI thread.
class Sc64Worker : public QObject
{
    Q_OBJECT
public:
    explicit Sc64Worker(QObject *parent = nullptr) : QObject(parent) {}
    ~Sc64Worker() override;

public slots:
    void refresh();
    void connectPort(const QString &port);
    void disconnectPort();
    void uploadRom(const QString &path);
    void listCard(const QString &path);
    void copyToCard(const QString &hostPath, const QString &cardPath);

signals:
    void devicesFound(const QStringList &ports);
    void connectionChanged(bool connected, const QString &info);
    void progress(double fraction);
    void uploadFinished(bool ok, const QString &message);
    void cardListed(const QVariantList &entries, const QString &message);

private:
    Sc64Device *m_device = nullptr;     // created lazily inside this thread
    Sc64FileSystem *m_fs = nullptr;
};

// GUI-facing object exposed to QML as the context property `sc64`.
class Sc64Controller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList cardEntries READ cardEntries NOTIFY cardEntriesChanged)
public:
    explicit Sc64Controller(QObject *parent = nullptr);
    ~Sc64Controller() override;

    QStringList devices() const { return m_devices; }
    bool connected() const { return m_connected; }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QVariantList cardEntries() const { return m_cardEntries; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectPort(const QString &port);
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void uploadRom(const QUrl &fileUrl);
    Q_INVOKABLE void refreshCard();
    Q_INVOKABLE void copyToCard(const QUrl &fileUrl);

signals:
    void devicesChanged();
    void connectedChanged();
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void cardEntriesChanged();

    // internal -> worker (queued across the thread boundary)
    void requestRefresh();
    void requestConnect(const QString &port);
    void requestDisconnect();
    void requestUpload(const QString &path);
    void requestListCard(const QString &path);
    void requestCopyToCard(const QString &hostPath, const QString &cardPath);

private:
    void setBusy(bool b);
    void setProgress(double p);
    void setStatus(const QString &s);

    QThread m_thread;
    Sc64Worker *m_worker = nullptr;

    QStringList m_devices;
    bool m_connected = false;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_status = QStringLiteral("Idle");
    QVariantList m_cardEntries;
};
