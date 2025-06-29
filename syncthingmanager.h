#ifndef SYNCTHINGMANAGER_H
#define SYNCTHINGMANAGER_H
#include "configHandler.h"
#include "apihandler.h"
#include "structbase.h"
#include "urlbase.h"
#include <QStorageInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QQueue>
#include <QTimer>
#include <QList>
#include <QMap>

#define WORKDIR "/work/bin/"
#define CONFIGDIR "/work/config/"
#define LOGDIR "/work/log/"
#define UPDATEPATH "/work/update"


class SyncthingManager : public QObject
{
    Q_OBJECT

public:



    // Static mapping: device ID -> set of Folders.
    QMap<QString, QSet<Folder>> deviceFolderMap;
    // Map to store device info.
    QMap<QString, Device> deviceInfoMap;

    //for Singleton Pattern
    ~SyncthingManager();
    SyncthingManager(const SyncthingManager&)=delete ;
    SyncthingManager & operator = (const SyncthingManager&)=delete;
    static SyncthingManager* getInstance();
    //    QJsonDocument getSystemLog();

    // start and stop health ping and events timer
    void startPingChecks(int intervalMs = 10000);
    void stopPingChecks();
    void startEventPolling(int intervalMs = 10000);
    void stopEventPolling();
    void startHealthChecks(int intervalMs = 10000);
    void stopHealthChecks();


    // High level methods
    void acceptDeviceConnection(Device device);
    void removeDevice(const QString &deviceId);
    void pauseDevice(const QString &deviceId);
    void resumeDevice(const QString &deviceId);
    void pauseFolder(const QString &folderId);
    void resumeFolder(const QString &folderId);
    void querySystemStatus();


    void handleDeviceConnectionRequest(Device device);
    void disconnectDevice(const QString &deviceId);
    void fetchDeviceId(const QString &deviceIp);

    void makeUpdate();
    void checkUpdaterConnection();
    void autoAcceptDeviceConnection(const Device &device);
    void configureLocalOnlyNode(const QString &deviceName, const QString &bindIp);
    void getMyDeviceId();
    void renameLocalDevice(const QString &newName);
    void connectToDeviceByIPv4(const QString &ipv4);
    void shareLocalFolder(const QString &folderPath);
    void shareLocalFolderIfNeeded(const QString &folderPath);
    void getSystemLog();
    void shareFolderWithConnectedDevices(const QString &folderId);
    void addDeviceToSharedFolder(const QString &deviceId);
    void acceptFolderSharing(Folder folder);
signals:
    // Emitted when the mapping is updated.
    void mappingUpdated(const QMap<QString, QSet<Folder>> &mapping);
    // Emitted when a new pending device connection is detected.
    void deviceConnectionRequested(Device device);
    // Emitted when a new Folder sharing requeswt is detected.
    void folderSharingRequested(Folder folder);


    // Emitted when a folder update is detected.
    void folderUpdated(const QString &deviceId, const QString &folderId);
    // Global error signal.
    void globalError(const QString &errorMessage);

    //High level signals
    void deviceAdded(const QString &deviceId);
    void deviceRemoved(const QString &deviceId);
    void devicePaused(const QString &deviceId);
    void deviceResumed(const QString &deviceId);
    void folderPaused(const QString &folderId);
    void folderResumed(const QString &folderId);
    void folderRescanned(const QString &folderId);
    void systemStatusReceived(const QJsonObject &status);

    // Health monitoring signals.
    void systemHealthCheck(bool isHealthy, const QString &reason);
    void pingPongStatus(bool isAlive);

    // Propagated errors and request info.
    void requestProcessed(const QString &info);

    //file and folder progress
    void folderSyncProgress(const QString &deviceId, const QString &folderId, double  percentage);
    void fileTransferProgress(const QString &deviceId, const QString &folderId, const QString &fileName, int percentage);



    // new update
    void updateAvailable();
    void updateDone();
    void deviceIdFetched(QString ip , QString id);
    void folderChangeInvitationReceived(QString deviceId,QString folderId);
    void otherDeviceConnected(bool remoteConnected);


private slots:
    void onDeviceAdded(const QString &deviceId);

    //Called periodically to check if Syncthing is online (via /rest/system/ping).
    void performPingCheck();


    //Called periodically to check if Syncthing is Health (via /rest/noauth/health).
    void performHealthCheck();

    //Called periodically to poll new events from /rest/events?since=lastEventId.
    void progressEvent();
    void pollSyncthing();

    void healthError();
private:

    QString myDeviceID;

    QString m_SharedFolderId;

    QString m_FolderID;
    QString m_DeviceID;
    bool serverConnected;
    bool IS_SERVER = false;
    SyncthingManager();
    ApiHandler * api;
    ConfigHandler  *co;
    QTimer m_healthTimer;
    QTimer m_eventsTimer;
    QTimer m_pingTimer;
    quint64 lastEventId;  // ID of last processed event for incremental polling
    QString m_allowedDeviceIp;  // Allowed device IP; others are denied.
    QString m_allowedDeviceID;
    // Last reported percentages to filter out redundant signals
    QHash<QString, int> lastFileProgress;    // Key: "device|folder|file"
    QHash<QString, int> lastFolderProgress;  // Key: "device|folder"

};

#endif // SYNCTHINGMANAGER_H
