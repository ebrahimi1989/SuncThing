#include "client_syncthingmanager.h"
#include "configHandler.h"

ClientSyncthingManager::ClientSyncthingManager(QObject* parent)
    : ISyncthingManager(parent)
    , impl(SyncthingManager::getInstance())
{
    /* --- special client policy --- */
    connect(impl, &SyncthingManager::folderSharingRequested,
            this, &ClientSyncthingManager::onFolderSharingInvitation);

    // connect to the server if not yet paired
    ConfigHandler * co = new ConfigHandler();
    const QString targetIp = co->getUpdatterAddress();        // add accessor
    impl->connectToDeviceByIPv4(targetIp);

    // forward common signals
    connect(impl, &SyncthingManager::updateDone,
            this, &ClientSyncthingManager::onUpdateDone);
    connect(impl, &SyncthingManager::globalError,
            this, &ISyncthingManager::globalError);
}

void ClientSyncthingManager::makeUpdate()        { impl->makeUpdate(); }
void ClientSyncthingManager::getMyDeviceId()     { impl->getMyDeviceId(); }
void ClientSyncthingManager::getSystemLog()      { impl->getSystemLog(); }
void ClientSyncthingManager::startHealthCheck()  { impl->startHealthChecks(); }

/* ----- client‑specific slots ----- */
void ClientSyncthingManager::onFolderSharingInvitation(Folder f)
{
    if (f.path == QString(UPDATEPATH))
        impl->acceptFolderSharing(f);
}

void ClientSyncthingManager::onUpdateDone()
{
    emit ISyncthingManager::updateDone();        // forward
}
