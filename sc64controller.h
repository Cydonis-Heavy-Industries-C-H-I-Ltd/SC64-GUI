#pragma once

#include "sc64device.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QVariantList>

class Sc64FileSystem;

// Lives in a worker thread and owns the device + filesystem.
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
    void makeDir(const QString &absPath, const QString &refreshPath);
    void renameEntry(const QString &absOld, const QString &absNew, const QString &refreshPath);
    void deleteEntry(const QString &absPath, const QString &refreshPath);
    void copyToCard(const QString &hostPath, const QString &cardPath, const QString &refreshPath);

signals:
    void devicesFound(const QStringList &ports);
    void connectionChanged(bool connected, const QString &info);
    void progress(double fraction);
    void uploadFinished(bool ok, const QString &message);
    // entries + the path they belong to + an optional action message
    void cardUpdated(const QVariantList &entries, const QString &path, const QString &message);

private:
    void relist(const QString &path, const QString &message);
    Sc64Device *m_device = nullptr;
    Sc64FileSystem *m_fs = nullptr;
};

// GUI-facing object exposed to QML as `sc64`.
class Sc64Controller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList cardEntries READ cardEntries NOTIFY cardEntriesChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY currentPathChanged)
public:
    explicit Sc64Controller(QObject *parent = nullptr);
    ~Sc64Controller() override;

    QStringList devices() const { return m_devices; }
    bool connected() const { return m_connected; }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QVariantList cardEntries() const { return m_cardEntries; }
    QString currentPath() const { return m_currentPath; }
    bool atRoot() const { return m_currentPath == QStringLiteral("/"); }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectPort(const QString &port);
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void uploadRom(const QUrl &fileUrl);

    Q_INVOKABLE void refreshCard();              // (re)list the current directory
    Q_INVOKABLE void openDir(const QString &name);
    Q_INVOKABLE void navigateUp();
    Q_INVOKABLE void makeDir(const QString &name);
    Q_INVOKABLE void renameEntry(const QString &oldName, const QString &newName);
    Q_INVOKABLE void deleteEntry(const QString &name);
    Q_INVOKABLE void copyToCard(const QUrl &fileUrl);

signals:
    void devicesChanged();
    void connectedChanged();
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void cardEntriesChanged();
    void currentPathChanged();

    // internal -> worker
    void requestRefresh();
    void requestConnect(const QString &port);
    void requestDisconnect();
    void requestUpload(const QString &path);
    void requestListCard(const QString &path);
    void requestMakeDir(const QString &absPath, const QString &refreshPath);
    void requestRename(const QString &absOld, const QString &absNew, const QString &refreshPath);
    void requestDelete(const QString &absPath, const QString &refreshPath);
    void requestCopyToCard(const QString &hostPath, const QString &cardPath, const QString &refreshPath);

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
    QString m_currentPath = QStringLiteral("/");
};
