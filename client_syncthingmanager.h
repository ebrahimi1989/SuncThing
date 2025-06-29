#ifndef CLIENT_SYNCTHINGMANAGER_H
#define CLIENT_SYNCTHINGMANAGER_H

#include "syncthingmanager_base.h"
#include "syncthingmanager.h"

class ClientSyncthingManager final : public ISyncthingManager
{
    Q_OBJECT
public:
    ClientSyncthingManager(QObject* p=nullptr);

    // shared interface
    void makeUpdate()           override;
    void getMyDeviceId()        override;
    void getSystemLog()         override;
    void startHealthCheck()     override;

private slots:
    void onFolderSharingInvitation(Folder f);
    void onUpdateDone();

private:
    SyncthingManager* impl;
};
#endif
