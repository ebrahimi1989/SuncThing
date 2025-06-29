#include "syncthingmanager_base.h"
#include "configHandler.h"              // for isServer()
#include "server_syncthingmanager.h"
#include "client_syncthingmanager.h"

static std::unique_ptr<ISyncthingManager>  g_instance;

ISyncthingManager* ISyncthingManager::instance()
{
    if (!g_instance)
    {
        ConfigHandler * co = new ConfigHandler() ;
        if (co->getWrapperIsServer())
            g_instance.reset(new ServerSyncthingManager);
        else
            g_instance.reset(new ClientSyncthingManager);
    }
    return g_instance.get();
}

void ISyncthingManager::destroy() { g_instance.reset(); }
