#include "server_syncthingmanager.h"

ServerSyncthingManager::ServerSyncthingManager(QObject* parent)
    : ISyncthingManager(parent)
    , impl(SyncthingManager::getInstance())      // keep original singleton
{
    // share /work/update with everyone at startup
    impl->shareLocalFolder(QString(UPDATEPATH));

    // ---- wire specialised server behaviour ----
    connect(impl, &SyncthingManager::deviceConnectionRequested,
            this, &ServerSyncthingManager::onDeviceConnectionRequested);
    connect(impl, &SyncthingManager::folderSharingRequested,
            this, &ServerSyncthingManager::onFolderShareRequested);

    // forward common signals
    connect(impl, &SyncthingManager::updateAvailable, this, &ISyncthingManager::updateAvailable);
    connect(impl, &SyncthingManager::updateDone,      this, &ISyncthingManager::updateDone);
    connect(impl, &SyncthingManager::globalError,     this, &ISyncthingManager::globalError);
}

/* =====  shared behaviour just forwards  ===== */
void ServerSyncthingManager::makeUpdate()        { impl->makeUpdate(); }
void ServerSyncthingManager::getMyDeviceId()     { impl->getMyDeviceId(); }
void ServerSyncthingManager::getSystemLog()      { impl->getSystemLog(); }
void ServerSyncthingManager::startHealthCheck()  { impl->startHealthChecks(); }

/* =====  server‑specific private slots  ===== */
void ServerSyncthingManager::onDeviceConnectionRequested(Device dev)
{
    impl->autoAcceptDeviceConnection(dev);       // auto‑accept
    impl->shareFolderWithConnectedDevices(QString(UPDATEPATH));
}

void ServerSyncthingManager::onFolderShareRequested(Folder f)
{
    impl->shareFolderWithConnectedDevices(f.id); // already sharing
}

void ServerSyncthingManager::onHealthOk() { /* … */ }
