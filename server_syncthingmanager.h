#ifndef SERVER_SYNCTHINGMANAGER_H
#define SERVER_SYNCTHINGMANAGER_H

#include "syncthingmanager_base.h"
#include "syncthingmanager.h"          // reuse existing logic
class ServerSyncthingManager final : public ISyncthingManager
{
    Q_OBJECT
public:
    ServerSyncthingManager(QObject* p=nullptr);

    // shared interface
    void makeUpdate()           override;
    void getMyDeviceId()        override;
    void getSystemLog()         override;
    void startHealthCheck()     override;

private slots:
    void onDeviceConnectionRequested(Device dev);
    void onFolderShareRequested(Folder f);
    void onHealthOk();                       // wrap existing slots

private:
    SyncthingManager* impl;                  // *delegation* not inheritance
};
#endif
