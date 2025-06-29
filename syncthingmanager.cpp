#include "syncthingmanager.h"
#include "configHandler.h"
#include "filehandler.h"
#include "validater.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QDebug>
#include <QUuid>
#include <QFileInfo>

static SyncthingManager* instance = nullptr;


SyncthingManager::~SyncthingManager()
{

}

SyncthingManager::SyncthingManager()
{
    co = new ConfigHandler();
    api = ApiHandler::getInstance();
    api->setApiKey(co->apiKey());
    api->setBaseUrl(co->baseUrl());
    IS_SERVER = co->getWrapperIsServer();
    if(IS_SERVER)
        shareLocalFolderIfNeeded(QString(UPDATEPATH));

    m_allowedDeviceIp = co->getUpdatterAddress();
    if(!IS_SERVER)
        connectToDeviceByIPv4(m_allowedDeviceIp);
    getMyDeviceId();
    lastEventId = co->getLastEvent();
    QString devName = co->getSyncName();
    configureLocalOnlyNode(devName, QString());

    // Propagate ApiHandler signals.
    connect(api, &ApiHandler::requestProcessed, this, &SyncthingManager::requestProcessed);
    connect(api, &ApiHandler::globalError, this, &SyncthingManager::globalError);
    connect(api, &ApiHandler::connectionError, this, &SyncthingManager::healthError);
    // Wire the timers to their respective slots
    connect(&m_pingTimer, &QTimer::timeout, this, &SyncthingManager::performPingCheck);
    connect(&m_healthTimer, &QTimer::timeout, this, &SyncthingManager::performHealthCheck);
    connect(&m_eventsTimer, &QTimer::timeout, this, &SyncthingManager::progressEvent);
    connect(&m_eventsTimer, &QTimer::timeout, this, &SyncthingManager::pollSyncthing);
    connect(this, &SyncthingManager::folderSharingRequested, this, &SyncthingManager::acceptFolderSharing);
    QObject::connect(this, &SyncthingManager::deviceConnectionRequested,
                     [&](Device device) {
        if(IS_SERVER){
            autoAcceptDeviceConnection(device);
            qDebug() << "Device connection requested from IP:" << device.ip;
        }
        //        handleDeviceConnectionRequest(device);// this was for cetain IP
    });
}

SyncthingManager* SyncthingManager::getInstance()
{
    if(instance == nullptr){
        instance= new SyncthingManager();
    }
    return instance;
}


void SyncthingManager::getSystemLog()
{
    // Prepare URLs
    QUrl logUrl = api->m_baseUrl;
    logUrl.setPath(QString(SYNCTHINGLOG));
    QUrl healthUrl = api->m_baseUrl;
    healthUrl.setPath(QString(HEALTH));

    // Step 1: Fetch system log
    ApiRequest logReq;
    logReq.method = ApiRequest::GET;
    logReq.url    = logUrl;
    logReq.callback = [this, healthUrl](QNetworkReply *replyLog) {
        if (replyLog->error() != QNetworkReply::NoError) {
            emit globalError(QString("Failed to fetch system log: %1")
                             .arg(replyLog->errorString()));
            replyLog->deleteLater();
            return;
        }

        // Parse the log JSON (or treat as raw if not JSON)
        QByteArray logData = replyLog->readAll();
        replyLog->deleteLater();
        QJsonDocument logDoc = QJsonDocument::fromJson(logData);
        QJsonObject combined;
        if (logDoc.isObject()) {
            combined = logDoc.object();
        } else {
            combined["log"] = QString::fromUtf8(logData);
        }

        // Step 2: Fetch health to get discoveryErrors
        ApiRequest healthReq;
        healthReq.method = ApiRequest::GET;
        healthReq.url    = healthUrl;
        healthReq.callback = [this, combined](QNetworkReply *replyHealth) mutable {
            if (replyHealth->error() == QNetworkReply::NoError) {
                QJsonDocument healthDoc = QJsonDocument::fromJson(replyHealth->readAll());
                if (healthDoc.isObject()) {
                    QJsonObject healthObj = healthDoc.object();
                    // Append discoveryErrors if present
                    if (healthObj.contains("discoveryErrors")) {
                        combined["discoveryErrors"] = healthObj.value("discoveryErrors");
                    }
                }
            } else {
                emit globalError(QString("Failed to fetch health info: %1")
                                 .arg(replyHealth->errorString()));
            }
            replyHealth->deleteLater();

            // Step 3: Write merged JSON to file
            bool ok = FileHandler::writeJsonToFile(combined);
            if (ok) {
                qDebug() << "[SyncthingManager] System log + discovery errors written.";
            } else {
                qWarning() << "[SyncthingManager] Failed to write system log file.";
            }
        };
        api->enqueueRequest(healthReq);
    };
    api->enqueueRequest(logReq);
}



void SyncthingManager::healthError(){
    emit systemHealthCheck(false, QString("Connection Error to Syncthing"));
}



//--------------------------
// Ping CHECKS
//--------------------------

void SyncthingManager::startPingChecks(int intervalMs)
{
    if (!m_pingTimer.isActive()) {
        m_pingTimer.start(intervalMs);
        qDebug() << "[SyncthingManager] Ping checks started, interval:" << intervalMs << "ms";
    }
}

void SyncthingManager::stopPingChecks()
{
    if (m_pingTimer.isActive()) {
        m_pingTimer.stop();
        qDebug() << "[SyncthingManager] Ping checks stopped.";
    }
}


//--------------------------
// Health CHECKS
//--------------------------

void SyncthingManager::startHealthChecks(int intervalMs)
{
    if (!m_healthTimer.isActive()) {
        m_healthTimer.start(intervalMs);
        qDebug() << "[SyncthingManager] Health checks started, interval:" << intervalMs << "ms";
    }
}

void SyncthingManager::stopHealthChecks()
{
    if (m_healthTimer.isActive()) {
        m_healthTimer.stop();
        qDebug() << "[SyncthingManager] Health checks stopped.";
    }
}

//--------------------------
// Health CHECKS
//--------------------------

void SyncthingManager::startEventPolling(int intervalMs)
{
    if (!m_eventsTimer.isActive()) {
        m_eventsTimer.start(intervalMs);
        qDebug() << "[SyncthingManager] Event polling started, interval:" << intervalMs << "ms";
    }
}

void SyncthingManager::stopEventPolling()
{
    if (m_eventsTimer.isActive()) {
        m_eventsTimer.stop();
        qDebug() << "[SyncthingManager] Event polling stopped.";
    }
}

void SyncthingManager::removeDevice(const QString &deviceId)
{
    QUrl reqUrl = api->m_baseUrl;
    reqUrl.setPath(QString("/rest/config/devices/%1").arg(deviceId));

    ApiRequest req;
    req.method = ApiRequest::DELETE_;
    req.url = reqUrl;
    req.callback = [this, deviceId](QNetworkReply *reply) {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status < 400) {
            qDebug() << "Device removed:" << deviceId;
            emit deviceRemoved(deviceId);
        }
    };
    api->enqueueRequest(req);
}

void SyncthingManager::pauseDevice(const QString &deviceId)
{
    QUrl reqUrl = api->m_baseUrl;
    reqUrl.setPath(QString(PAUSEDEVICE));
    QUrlQuery query;
    query.addQueryItem("device", deviceId);
    reqUrl.setQuery(query);

    ApiRequest req;
    req.method = ApiRequest::POST;
    req.url = reqUrl;
    req.callback = [this, deviceId](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug() << "Device paused:" << deviceId;
            emit devicePaused(deviceId);
        }
    };
    api->enqueueRequest(req);
}

void SyncthingManager::resumeDevice(const QString &deviceId)
{
    QUrl reqUrl = api->m_baseUrl;
    reqUrl.setPath(QString(RESUMEDEVICE));
    QUrlQuery query;
    query.addQueryItem("device", deviceId);
    reqUrl.setQuery(query);

    ApiRequest req;
    req.method = ApiRequest::POST;
    req.url = reqUrl;
    req.callback = [this, deviceId](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug() << "Device resumed:" << deviceId;
            emit deviceResumed(deviceId);
        }
    };
    api->enqueueRequest(req);
}

void SyncthingManager::pauseFolder(const QString &folderId)
{
    QUrl reqUrl = api->m_baseUrl;
    reqUrl.setPath(QString(QString(CONFIGFOLDER)+"/"+folderId));

    QJsonObject payloadObj;
    payloadObj["paused"] = true;
    QByteArray payload = QJsonDocument(payloadObj).toJson();

    ApiRequest req;
    req.method = ApiRequest::PATCH;
    req.url = reqUrl;
    req.payload = payload;
    req.callback = [this, folderId](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug() << "Folder paused:" << folderId;
            emit folderPaused(folderId);
        }
    };
    api->enqueueRequest(req);
}

void SyncthingManager::resumeFolder(const QString &folderId)
{
    QUrl reqUrl = api->m_baseUrl;
    reqUrl.setPath(QString(QString(CONFIGFOLDER)+"/"+folderId));

    QJsonObject payloadObj;
    payloadObj["paused"] = false;
    QByteArray payload = QJsonDocument(payloadObj).toJson();

    ApiRequest req;
    req.method = ApiRequest::PATCH;
    req.url = reqUrl;
    req.payload = payload;
    req.callback = [this, folderId](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug() << "Folder resumed:" << folderId;
            emit folderResumed(folderId);
        }
    };
    api->enqueueRequest(req);
}

void SyncthingManager::querySystemStatus()
{
    QUrl reqUrl = api->m_baseUrl;
    reqUrl.setPath(QString(STATUS));

    ApiRequest req;
    req.method = ApiRequest::GET;
    req.url = reqUrl;
    req.callback = [this](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                QJsonObject status = doc.object();
                qDebug() << "System Status:" << QJsonDocument(status).toJson(QJsonDocument::Compact);
                emit systemStatusReceived(status);
            }
        }
    };
    api->enqueueRequest(req);
}

// ----- Health Monitoring and Ping Checks -----

void SyncthingManager::performHealthCheck()
{
    QUrl healthUrl = api->m_baseUrl;
    healthUrl.setPath(QString(HEALTH));  // "/rest/noauth/health"

    ApiRequest healthReq;
    healthReq.method = ApiRequest::GET;
    healthReq.url    = healthUrl;
    healthReq.callback = [this](QNetworkReply *replyHealth) {
        if (replyHealth->error() != QNetworkReply::NoError) {
            replyHealth->deleteLater();
            emit systemHealthCheck(false, QStringLiteral("syncthing is down"));
            return;
        }

        QByteArray hData = replyHealth->readAll();
        replyHealth->deleteLater();
        QJsonParseError hErr;
        QJsonDocument hDoc = QJsonDocument::fromJson(hData, &hErr);
        if (hErr.error != QJsonParseError::NoError || !hDoc.isObject()) {
            emit systemHealthCheck(false, QStringLiteral("syncthing is down"));
            return;
        }

        QJsonObject hObj = hDoc.object();
        QString state = hObj.value("status").toString();

        // Healthy case now uses empty reason
        if (state == QLatin1String("OK")) {
            emit systemHealthCheck(true, QString());
            return;
        }

        // Non‑OK: fetch full status for discoveryErrors
        QUrl statusUrl = api->m_baseUrl;
        statusUrl.setPath(QString(STATUS));  // "/rest/system/status"

        ApiRequest statusReq;
        statusReq.method = ApiRequest::GET;
        statusReq.url    = statusUrl;
        statusReq.callback = [this, state](QNetworkReply *replyStatus) {
            QString errorInfo;

            if (replyStatus->error() == QNetworkReply::NoError) {
                QJsonDocument sDoc = QJsonDocument::fromJson(replyStatus->readAll());
                if (sDoc.isObject()) {
                    QJsonObject sObj = sDoc.object();
                    if (sObj.contains("discoveryErrors") &&
                            sObj.value("discoveryErrors").isObject()) {
                        QJsonObject errs = sObj.value("discoveryErrors").toObject();
                        if (!errs.isEmpty()) {
                            errorInfo = QString::fromUtf8(
                                        QJsonDocument(errs).toJson(QJsonDocument::Compact));
                        }
                    }
                }
            }
            replyStatus->deleteLater();

            if (errorInfo.isEmpty())
                errorInfo = state;

            emit systemHealthCheck(false, errorInfo);
        };

        api->enqueueRequest(statusReq);
    };

    api->enqueueRequest(healthReq);
}

void SyncthingManager::performPingCheck()
{
    QUrl reqUrl = api->m_baseUrl;
    reqUrl.setPath(QString(PING));

    ApiRequest req;
    req.method = ApiRequest::GET;
    req.url = reqUrl;
    req.callback = [this](QNetworkReply *reply) {
        bool alive = (reply->error() == QNetworkReply::NoError);
        qDebug() << "[Ping Check]:" << (alive ? "Alive" : "Dead");
        emit pingPongStatus(alive);
    };
    api->enqueueRequest(req);
}


void SyncthingManager::acceptDeviceConnection(Device device)
{
    // Send a PUT (or POST) to add the device.
    QUrl url = api->m_baseUrl;
    url.setPath(QString(CONFIGDEVICE));
    QJsonArray arr;
    QJsonObject obj;
    obj["deviceID"] = device.id;
    if (!device.devName.isEmpty())
        obj["name"] = device.devName;
    if (!device.ip.isEmpty()){
        QJsonArray addr;
        //        addr.append("tcp://"+m_allowedDeviceIp);
        obj["address"] = addr;
    }
    arr.append(obj);
    QByteArray payload = QJsonDocument(arr).toJson();
    ApiRequest req;
    req.method = ApiRequest::PUT;
    req.url = url;
    req.payload = payload;
    req.callback = [=](QNetworkReply *reply) {
        qDebug()<<"Im here  req.callback = [=](QNetworkReply *reply) {";
        if (!reply->error()) {
            qDebug() << "Device" << device.ip << "accepted.";
            m_DeviceID = device.id;
        }
    };
    api->enqueueRequest(req);
}



void SyncthingManager::acceptFolderSharing(Folder  folder)
{
    // Send a PUT (or POST) to add the device.
    QUrl url = api->m_baseUrl;
    url.setPath(QString(CONFIGFOLDER));

    QJsonObject deviceO;
    deviceO["deviceID"]= m_allowedDeviceID;

    QJsonArray da ;
    da.append(deviceO);
    QJsonArray arr;
    QJsonObject obj;
    QJsonObject jsonPayload;
    jsonPayload["id"] = folder.id;            // Should match device A’s folder ID.
    jsonPayload["path"] = QString(UPDATEPATH);//folder.path;          // Local mapping on device B.
    jsonPayload["type"] = "receiveonly";       // Use "sendreceive" for two-way sync, or "receiveonly" to only download changes.
    jsonPayload["lable"] = folder.label;
    jsonPayload["devices"] = da;


    QJsonArray rootArray;
    rootArray.append(jsonPayload);
    arr.append(obj);
    QByteArray payload = QJsonDocument(rootArray).toJson();
    ApiRequest req;
    req.method = ApiRequest::PUT;
    req.url = url;
    req.payload = payload;
    req.callback = [=](QNetworkReply *reply) {
        if (!reply->error()) {
            qDebug() << "Folder " << folder.id << "accepted.";
            m_FolderID = folder.id;
        }
    };
    api->enqueueRequest(req);
}


void SyncthingManager::pollSyncthing() {
    // Step 3: Poll pending device connections.
    if (IS_SERVER){
        if (!serverConnected){
            QUrl pendingUrl = api->m_baseUrl;
            pendingUrl.setPath(QString(PENDINGDEVICE));
            ApiRequest reqPending;
            reqPending.method = ApiRequest::GET;
            reqPending.url = pendingUrl;
            reqPending.callback = [=](QNetworkReply *replyPending) {
                //        if (!replyPending->error()) {
                QJsonDocument pendDoc = QJsonDocument::fromJson(replyPending->readAll());
                //            if (pendDoc.isObject()) {

                QJsonObject pendObj = pendDoc.object();
                auto list = pendObj.keys();
                if(!list.isEmpty()){
                    auto last = list.last();
                    qDebug()<<last;

                    for (const QString &devId : pendObj.keys()) {
                        qDebug()<<"deviceConnectionRequested"<<devId;
                        Device d;//to do fill that
                        d.id = devId;
                        auto obj = pendObj.value(devId).toObject();
                        d.ip = obj.value("address").toString();
                        d.devName = obj.value("name").toString();
                        emit deviceConnectionRequested(d);
                    }
                }
                //            }
                //        }
            };
            api->enqueueRequest(reqPending);
        }
    }
    // step4 : check folder update

    if(!IS_SERVER){
        // Step 3: Poll pending device connections.
        QUrl pendingFUrl = api->m_baseUrl;
        pendingFUrl.setPath(QString(PENDINGFOLDSERS));
        ApiRequest reqFPending;
        reqFPending.method = ApiRequest::GET;
        reqFPending.url = pendingFUrl;
        reqFPending.callback = [=](QNetworkReply *replyPending) {
            QJsonDocument pendDoc = QJsonDocument::fromJson(replyPending->readAll());
            QJsonObject rootObj = pendDoc.object();
            QList<Folder> folders;
            for (const QString &folderKey : rootObj.keys()) {
                QJsonObject folderObj = rootObj.value(folderKey).toObject();
                // Parse the "offeredBy" object.
                Folder folder;
                folder.id = folderKey;
                if (folderObj.contains("offeredBy") && folderObj.value("offeredBy").isObject()) {
                    QJsonObject offeredByObj = folderObj.value("offeredBy").toObject();
                    for (const QString &deviceKey : offeredByObj.keys()) {
                        QJsonObject offerObj = offeredByObj.value(deviceKey).toObject();
                        // Parse the "time" field.
                        if (offerObj.contains("time") && offerObj.value("time").isString()) {
                            QString timeStr = offerObj.value("time").toString();
                            folder.lastUpdatedTime = QDateTime::fromString(timeStr, Qt::ISODate);

                        } else {
                            folder.lastUpdatedTime = QDateTime(); // default to an invalid time
                            qWarning() << "Missing or invalid 'time' for device:" << deviceKey;
                        }

                        // Parse the "label" field.
                        if (offerObj.contains("label") && offerObj.value("label").isString()) {
                            folder.label = offerObj.value("label").toString();
                        } else {
                            auto label = QString();
                            qWarning() << "Missing 'label' for device:" << deviceKey;
                        }

                        // Parse the encryption fields.
                        //todo adding to top of Folder struct when using encription features
                        auto receiveEncrypted = offerObj.contains("receiveEncrypted") ?
                                    offerObj.value("receiveEncrypted").toBool() : false;
                        auto remoteEncrypted = offerObj.contains("remoteEncrypted") ?
                                    offerObj.value("remoteEncrypted").toBool() : false;

                        // Optionally parse the "path" field if provided (this may indicate the desired destination).
                        if (offerObj.contains("path") && offerObj.value("path").isString()) {
                            folder.path= offerObj.value("path").toString();
                        } else {
                            folder.path = QString();
                        }

                        //                    pendingFolder.offers.append(offer);
                    }
                } else {
                    qWarning() << "No 'offeredBy' object found for folder:" << folderKey;
                }
                folders.append(folder);
            }
            if (!folders.empty())
                emit folderSharingRequested(folders.last());

        };
        api->enqueueRequest(reqFPending);
    }
    if (!IS_SERVER)
        checkUpdaterConnection();


    if(IS_SERVER){
        if (!m_SharedFolderId.isEmpty())
            shareFolderWithConnectedDevices(m_SharedFolderId);
    }
}

void SyncthingManager::progressEvent()
{
    QString eventsUrl = QString("%1/rest/events?since=%2")
            .arg(api->m_baseUrl.toString()).arg(lastEventId);
    ApiRequest req;
    req.method = ApiRequest::GET;
    req.url = QUrl(eventsUrl);
    req.callback = [this](QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit globalError(QString("Events poll error: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        QByteArray data = reply->readAll();
        reply->deleteLater();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit globalError(QString("Events JSON parse error: %1").arg(parseError.errorString()));
            return;
        }
        if (!doc.isArray()) {
            return;
        }
        QJsonArray events = doc.array();
        bool newUpdate = !events.isEmpty();
        for (const QJsonValue &evVal : events) {
            QJsonObject ev = evVal.toObject();
            quint64 id = static_cast<quint64>(ev.value("id").toDouble());
            if (id > lastEventId){
                lastEventId = id;
                co->setLastEvent(id);
            }
            QString type = ev.value("type").toString();
            QJsonObject dataObj = ev.value("data").toObject();
            /*            if (type == "DownloadProgress") {
                if (dataObj.isEmpty()) {
                    lastFileProgress.clear();
                } else {
                    for (auto folderIt = dataObj.constBegin(); folderIt != dataObj.constEnd(); ++folderIt) {
                        QString folderId = folderIt.key();
                        QJsonObject filesObj = folderIt.value().toObject();
                        for (auto fileIt = filesObj.constBegin(); fileIt != filesObj.constEnd(); ++fileIt) {
                            QString fileName = fileIt.key();
                            QJsonObject fileInfo = fileIt.value().toObject();
                            int percent = 0;
                            if (fileInfo.contains("bytesTotal") && fileInfo.contains("bytesDone")) {
                                qint64 total = static_cast<qint64>(fileInfo.value("bytesTotal").toDouble());
                                qint64 done  = static_cast<qint64>(fileInfo.value("bytesDone").toDouble());
                                if (total > 0)
                                    percent = static_cast<int>((done * 100) / total);
                            }
                            if (percent < 0) percent = 0;
                            if (percent > 100) percent = 100;
                            QString key = QString("local|%1|%2").arg(folderId).arg(fileName);
                            int lastPerc = lastFileProgress.value(key, -1);
                            if (percent != lastPerc) {
                                emit fileTransferProgress("local", folderId, fileName, percent);
                                lastFileProgress[key] = percent;
                            }
                        }
                    }
                }
            } else*/
            if (!IS_SERVER){
                if (type == "FolderCompletion") {
                    QString folderId = dataObj.value("folder").toString();
                    QString deviceId = dataObj.value("device").toString();
                    int completion = dataObj.value("completion").toInt();
                    if(completion == 100)
                        emit updateDone();
                    QString key = deviceId + "|" + folderId;
                    int lastPerc = lastFolderProgress.value(key, -1);
                    if (completion != lastPerc) {
                        //                    emit folderSyncProgress(deviceId, folderId, completion);
                        lastFolderProgress[key] = completion;
                    }
                }
            }/*else if (type == "RemoteIndexUpdated") {
                pauseFolder(m_FolderID);
                emit updateAvailable();
            }*/else{
                qDebug()<<"event type is "<<type;
            }
        }
    };
    api->enqueueRequest(req);
}


void SyncthingManager::handleDeviceConnectionRequest(Device device)
{

    if (device.ip == m_allowedDeviceIp) {
        qDebug() << "Allowed device connection from IP:" << device.ip;
        // Optionally, accept the connection.
        acceptDeviceConnection(device);
    } else {
        qDebug() << "Device with IP" << device.ip << "is not allowed. Disconnecting...";
        QString devId;
        for (auto it = deviceInfoMap.begin(); it != deviceInfoMap.end(); ++it) {
            if (it.value().ip == device.ip) {
                devId = it.key();
                break;
            }
        }
        if (!devId.isEmpty()) {
            disconnectDevice(devId);
        } else {
            fetchDeviceId(device.ip);
            auto deviceIp = device.ip;
            connect(this, &SyncthingManager::deviceIdFetched,
                    [this, deviceIp](const QString &ip, const QString &devId) {
                if (ip == deviceIp && !devId.isEmpty()) {
                    disconnectDevice(devId);
                }
            });
        }
    }
}
void SyncthingManager::disconnectDevice(const QString &deviceId)
{
    QUrl url = api->m_baseUrl;
    url.setPath(QString(DISCONNECTDEVICE ));
    QUrlQuery query;
    query.addQueryItem("device", deviceId);
    url.setQuery(query);
    ApiRequest req;
    req.method = ApiRequest::POST;
    req.url = url;
    req.callback = [=](QNetworkReply *reply) {
        if (!reply->error())
            qDebug() << "Device" << deviceId << "disconnected.";
        else
            emit globalError(QString("Failed to disconnect device %1: %2").arg(deviceId, reply->errorString()));
        reply->deleteLater();
    };
    api->enqueueRequest(req);
}
void SyncthingManager::fetchDeviceId(const QString &deviceIp)
{
    QUrl devicesUrl = api->m_baseUrl;
    devicesUrl.setPath(QString(CONFIGDEVICE));
    ApiRequest req;
    req.method = ApiRequest::GET;
    req.url = devicesUrl;
    req.callback = [this, deviceIp](QNetworkReply *reply) {
        if (reply->error()) {
            emit globalError(QString("fetchDeviceId error: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        QJsonArray arr = doc.array();
        QString foundId;
        for (const QJsonValue &val : arr) {
            if (!val.isObject())
                continue;
            QJsonObject obj = val.toObject();
            QString devId = obj["deviceID"].toString();
            QJsonArray addrs = obj["addresses"].toArray();
            for (const QJsonValue &a : addrs) {
                QString addr = a.toString();
                if (addr.contains(deviceIp) && addr != "dynamic") {
                    foundId = devId;
                    break;
                }
            }
            if (!foundId.isEmpty())
                break;
        }
        if (!foundId.isEmpty())
            emit deviceIdFetched(deviceIp, foundId);
        else
            emit globalError(QString("Device ID not found for IP: %1").arg(deviceIp));
    };
    api->enqueueRequest(req);
}



void SyncthingManager::makeUpdate()
{
    resumeFolder(m_FolderID);
}


void SyncthingManager::checkUpdaterConnection()
{
    QUrl clusterStatusUrl = api->m_baseUrl;
    clusterStatusUrl.setPath(QString(CONNECTEDDEVICE));
    ApiRequest req;
    req.method = ApiRequest::GET;
    req.url = clusterStatusUrl;
    req.callback = [this](QNetworkReply *reply) {
        if (reply->error()) {
            emit globalError(QString("Cluster status error: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        if (!doc.isObject()) {
            emit globalError("Cluster status response is not a JSON object.");
            return;
        }
        bool remoteConnected = false;
        // Iterate over each device entry in the JSON.
        QJsonObject root = doc.object();
        QJsonObject connections = root.value("connections").toObject();
        for (auto it = connections.begin(); it != connections.end(); ++it) {
        auto key = it.key();
        m_allowedDeviceID = key;
            QJsonObject deviceObj = it.value().toObject();
            if (deviceObj.value("connected").toBool()/*&&deviceObj.value("address").toString()==m_allowedDeviceIp*/)
                remoteConnected = true;
        }
        serverConnected = remoteConnected;
        if (!remoteConnected)
            connectToDeviceByIPv4(m_allowedDeviceIp);
        emit otherDeviceConnected(remoteConnected);
    };
    api->enqueueRequest(req);
}

//only for server

void SyncthingManager::autoAcceptDeviceConnection(const Device& device)
{
    // Validate input
    if (device.id.isEmpty() || device.ip.isEmpty()) {
        qWarning() << "[SyncthingManager] Invalid device info, skipping auto-accept.";
        return;
    }

    // Step 1: Construct device config object
    QJsonObject deviceObj;
    deviceObj["deviceID"] = device.id;
    deviceObj["name"] = device.devName.isEmpty()
            ? QString("NewDevice-%1").arg(device.id.right(4))
            : device.devName;

    // Step 2: Use IPv4 address only
    QJsonArray addrArr;
    addrArr.append("tcp://" + device.ip);
    deviceObj["addresses"] = addrArr;

    // Step 3: Fetch current config
    QUrl url = api->m_baseUrl;
    url.setPath(QString(CONFIG));

    ApiRequest getConfig;
    getConfig.method = ApiRequest::GET;
    getConfig.url = url;
    getConfig.callback = [this, deviceObj, url](QNetworkReply *reply) {
        if (reply->error()) {
            emit globalError("Failed to fetch config: " + reply->errorString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();

        QJsonArray devices = root["devices"].toArray();

        // Prevent duplicate entries
        for (const auto& val : devices) {
            if (val.toObject().value("deviceID") == deviceObj["deviceID"]) {
                qDebug() << "[SyncthingManager] Device already trusted:" << deviceObj["deviceID"].toString();
                return;
            }
        }

        devices.append(deviceObj);
        root["devices"] = devices;

        QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

        ApiRequest setConfig;
        setConfig.method = ApiRequest::POST;
        setConfig.url = url;
        setConfig.payload = payload;
        setConfig.callback = [this, deviceObj](QNetworkReply *setReply) {
            if (!setReply->error()) {
                qDebug() << "[SyncthingManager] Auto-accepted device:" << deviceObj["deviceID"].toString();
            } else {
                emit globalError("Failed to apply config: " + setReply->errorString());
            }
        };

        api->enqueueRequest(setConfig);
    };

    api->enqueueRequest(getConfig);
}


//for evry device
void SyncthingManager::configureLocalOnlyNode(const QString& deviceName, const QString& bindIp)
{
    QUrl url = api->m_baseUrl;
    url.setPath(QString(CONFIG));

    ApiRequest getConfig;
    getConfig.method = ApiRequest::GET;
    getConfig.url = url;
    getConfig.callback = [this, url, deviceName, bindIp](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit globalError("Failed to get system config: " + reply->errorString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject config = doc.object();

        // Step 1: Disable global discovery, NAT traversal, relaying
        QJsonObject options = config["options"].toObject();
        options["globalAnnounceEnabled"] = false;
        options["natEnabled"] = false;
        options["relaysEnabled"] = false;
        options["localAnnounceEnabled"] = true;

        // Step 2: Set listen address (sync protocol)
        QJsonArray listenAddresses;
        listenAddresses.append("tcp://" + (bindIp.isEmpty() ? "0.0.0.0:22000" : bindIp));
        options["listenAddresses"] = listenAddresses;

        // Step 3: Set device name
        options["deviceName"] = deviceName;

        config["options"] = options;

        QByteArray updatedConfig = QJsonDocument(config).toJson(QJsonDocument::Compact);

        ApiRequest setConfig;
        setConfig.method = ApiRequest::POST;
        setConfig.url = url;
        setConfig.payload = updatedConfig;
        setConfig.callback = [this](QNetworkReply* setReply) {
            if (setReply->error() == QNetworkReply::NoError) {
                qDebug() << "[SyncthingManager] Node configured for local-only sync.";
            } else {
                emit globalError("Failed to set config: " + setReply->errorString());
            }
        };

        api->enqueueRequest(setConfig);
    };

    api->enqueueRequest(getConfig);
}

void SyncthingManager::getMyDeviceId()
{
    QUrl url = api->m_baseUrl;
    url.setPath(QString(MYSTATUS ));
    ApiRequest req;
    req.method = ApiRequest::POST;
    req.url = url;
    req.callback = [=](QNetworkReply *reply) {
        if (!reply->error()){
            auto response = reply->readAll();
            auto doc = QJsonDocument::fromJson(response);
            if (!doc.isNull()&&doc.isObject()){
                auto obj = doc.object();
                if(obj.contains("myID"))
                    myDeviceID = obj["myID"].toString();
                if( !myDeviceID.isEmpty() )
                    qDebug()<<"my Devicde ID is ......";
            }
        }else
            reply->deleteLater();
    };
    api->enqueueRequest(req);
}



void SyncthingManager::renameLocalDevice(const QString &newName)
{
    // Step 1: Get local device ID from /rest/system/status
    QUrl statusUrl = api->m_baseUrl;
    statusUrl.setPath(QString(MYSTATUS));

    ApiRequest statusReq;
    statusReq.method = ApiRequest::GET;
    statusReq.url    = statusUrl;
    statusReq.callback = [this, newName](QNetworkReply *statusReply) {
        if (statusReply->error() != QNetworkReply::NoError) {
            emit globalError("Failed to get system status: " + statusReply->errorString());
            statusReply->deleteLater();
            return;
        }
        // Parse out "myID"
        QByteArray statusData = statusReply->readAll();
        statusReply->deleteLater();
        QJsonDocument statusDoc = QJsonDocument::fromJson(statusData);
        if (!statusDoc.isObject()) {
            emit globalError("Unexpected status format");
            return;
        }
        QString localId = statusDoc.object().value("myID").toString();
        if (localId.isEmpty()) {
            emit globalError("Local device ID not found in status");
            return;
        }

        // Step 2: Get full config from /rest/system/config
        QUrl configUrl = api->m_baseUrl;
        configUrl.setPath("/rest/system/config");
        ApiRequest getCfgReq;
        getCfgReq.method = ApiRequest::GET;
        getCfgReq.url    = configUrl;
        getCfgReq.callback = [this, newName, localId, configUrl](QNetworkReply *cfgReply) {
            if (cfgReply->error() != QNetworkReply::NoError) {
                emit globalError("Failed to fetch config: " + cfgReply->errorString());
                cfgReply->deleteLater();
                return;
            }
            // Parse config JSON
            QByteArray cfgData = cfgReply->readAll();
            cfgReply->deleteLater();
            QJsonDocument cfgDoc = QJsonDocument::fromJson(cfgData);
            if (!cfgDoc.isObject()) {
                emit globalError("Unexpected config format");
                return;
            }
            QJsonObject cfgObj = cfgDoc.object();

            // Step 3: Update the "name" field for the local device
            QJsonArray devices = cfgObj.value("devices").toArray();
            bool found = false;
            for (int i = 0; i < devices.size(); ++i) {
                QJsonObject dev = devices.at(i).toObject();
                if (dev.value("deviceID").toString() == localId) {
                    dev["name"] = newName;
                    devices.replace(i, dev);
                    found = true;
                    break;
                }
            }
            if (!found) {
                emit globalError("Local device entry not found in config");
                return;
            }
            cfgObj["devices"] = devices;

            // Step 4: POST the modified config back
            QByteArray payload = QJsonDocument(cfgObj).toJson(QJsonDocument::Compact);
            ApiRequest setCfgReq;
            setCfgReq.method  = ApiRequest::POST;
            setCfgReq.url     = configUrl;
            setCfgReq.payload = payload;
            setCfgReq.callback = [this](QNetworkReply *setReply) {
                if (setReply->error() != QNetworkReply::NoError) {
                    emit globalError("Failed to apply config: " + setReply->errorString());
                } else {
                    qDebug() << "[SyncthingManager] Renamed device successfully.";
                    emit requestProcessed("Local device renamed to deviceID");
                }
                setReply->deleteLater();
            };
            api->enqueueRequest(setCfgReq);
        };
        api->enqueueRequest(getCfgReq);
    };
    api->enqueueRequest(statusReq);
}




void SyncthingManager::connectToDeviceByIPv4(const QString &ipPort)
{
    // Step 1: GET /rest/system/discovery
    QUrl discUrl = api->m_baseUrl;
    discUrl.setPath(QString(DISCOVERY));

    ApiRequest discReq;
    discReq.method = ApiRequest::GET;
    discReq.url    = discUrl;
    discReq.callback = [this, ipPort](QNetworkReply *discReply) {
        if (discReply->error() != QNetworkReply::NoError) {
            emit globalError(
                        QString("Failed to fetch discovery: %1")
                        .arg(discReply->errorString()));
            discReply->deleteLater();
            return;
        }

        // Parse JSON: { deviceID: { "addresses": [ ... ] }, ... }
        QJsonDocument doc = QJsonDocument::fromJson(discReply->readAll());
        discReply->deleteLater();
        if (!doc.isObject()) {
            emit globalError("Discovery returned unexpected format");
            return;
        }
        QJsonObject root = doc.object();

        // Step 2: find deviceID matching ipPort
        QString foundId, foundAddr;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const QString deviceId = it.key();
            QJsonObject entry = it.value().toObject();
            QJsonArray addrs = entry.value("addresses").toArray();
            for (const QJsonValue &v : addrs) {
                QString raw = v.toString();
                // strip any scheme
                QString candidate = raw;
                if (candidate.startsWith("tcp://"))
                    candidate = candidate.mid(6);
                else if (candidate.startsWith("tcp4://"))
                    candidate = candidate.mid(7);

                if (candidate == ipPort) {
                    // preserve original scheme if present
                    if (raw.startsWith("tcp4://"))
                        foundAddr = raw;
                    else
                        foundAddr = QString("tcp://%1").arg(candidate);
                    foundId = deviceId;
                    break;
                }
            }
            if (!foundId.isEmpty()) break;
        }

        if (foundId.isEmpty()) {
            emit globalError(
                        QString("No device found advertising %1").arg(ipPort));
            return;
        }

        // Step 3: PUT /rest/config/devices with single-element array
        QUrl cfgUrl = api->m_baseUrl;
        cfgUrl.setPath(QString(REQUESTCONNECTION));

        QJsonObject devObj;
        devObj["deviceID"]  = foundId;
        // include address override for this peer
        devObj["addresses"] = QJsonArray{ foundAddr };
        // optional: preserve known name
        QString name = deviceInfoMap.value(foundId).devName;
        if (!name.isEmpty())
            devObj["name"] = name;

        QJsonArray payloadArr;
        payloadArr.append(devObj);

        ApiRequest putReq;
        putReq.method  = ApiRequest::PUT;
        putReq.url     = cfgUrl;
        putReq.payload = QJsonDocument(payloadArr).toJson(QJsonDocument::Compact);
        putReq.callback = [this, foundId, foundAddr](QNetworkReply *putReply) {
            if (putReply->error() == QNetworkReply::NoError) {
                qDebug() << "[SyncthingManager] Sent connection request to"
                         << foundId << "@" << foundAddr;
                emit deviceAdded(foundId);
            } else {
                emit globalError(
                            QString("Connect-to-device failed: %1")
                            .arg(putReply->errorString()));
            }
            putReply->deleteLater();
        };

        api->enqueueRequest(putReq);
    };

    api->enqueueRequest(discReq);
}






void SyncthingManager::shareLocalFolder(const QString &folderPath)
{
    // Derive a safe folder ID (UUID without braces)
    QString folderId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Use the folder’s base name as the label
    QString label = QFileInfo(folderPath).fileName();

    // 1) Fetch full config
    QUrl cfgUrl = api->m_baseUrl;
    cfgUrl.setPath(QString(CONFIG));

    ApiRequest getReq;
    getReq.method = ApiRequest::GET;
    getReq.url    = cfgUrl;
    getReq.callback = [this, folderId, folderPath, label, cfgUrl](QNetworkReply *getReply) {
        if (getReply->error() != QNetworkReply::NoError) {
            emit globalError(
                        QString("Failed to fetch config: %1")
                        .arg(getReply->errorString()));
            getReply->deleteLater();
            return;
        }

        // Parse JSON
        QByteArray data = getReply->readAll();
        getReply->deleteLater();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit globalError(
                        QString("Invalid config JSON: %1")
                        .arg(parseErr.errorString()));
            return;
        }
        QJsonObject cfg = doc.object();

        // Ensure "folders" exists
        if (!cfg.contains("folders") || !cfg["folders"].isArray()) {
            emit globalError("Config missing \"folders\" array");
            return;
        }
        QJsonArray folders = cfg["folders"].toArray();

        // 2) Build and append new folder entry
        QJsonObject newFolder;
        newFolder["id"]      = folderId;
        newFolder["path"]    = folderPath;
        newFolder["label"]   = label;
        newFolder["type"]    = "sendreceive";
        newFolder["devices"] = QJsonArray();  // share to nobody initially
        folders.append(newFolder);
        cfg["folders"] = folders;

        // 3) POST updated config
        QByteArray payload = QJsonDocument(cfg).toJson(QJsonDocument::Compact);
        ApiRequest postReq;
        postReq.method  = ApiRequest::POST;
        postReq.url     = cfgUrl;
        postReq.payload = payload;
        postReq.callback = [this, folderId](QNetworkReply *postReply) {
            if (postReply->error() == QNetworkReply::NoError) {
                qDebug() << "[SyncthingManager] Shared folder added:" << folderId;
                m_FolderID = folderId;
                m_SharedFolderId = m_FolderID;
                emit folderRescanned(folderId);

            } else {
                emit globalError(
                            QString("Failed to add folder \"%1\": %2")
                            .arg(folderId, postReply->errorString()));
            }
            postReply->deleteLater();
        };

        api->enqueueRequest(postReq);
    };

    api->enqueueRequest(getReq);
}


void SyncthingManager::shareLocalFolderIfNeeded(const QString &folderPath)
{
    // 1) Fetch current config
    QUrl cfgUrl = api->m_baseUrl;
    cfgUrl.setPath(QString(CONFIG));

    ApiRequest cfgReq;
    cfgReq.method = ApiRequest::GET;
    cfgReq.url    = cfgUrl;
    cfgReq.callback = [this, folderPath](QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit globalError(
                        QString("Failed to fetch config: %1").arg(reply->errorString())
                        );
            reply->deleteLater();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        if (!doc.isObject()) {
            emit globalError("Invalid config JSON");
            return;
        }

        QJsonObject root = doc.object();
        QJsonArray folders = root.value("folders").toArray();

        // 2) Check if any folder.path matches
        for (const QJsonValue &val : folders) {
            QJsonObject fo = val.toObject();
            if (fo.value("path").toString() == folderPath) {
                // keep folder ID
                qDebug() << "Folder already shared:" << folderPath;
                m_FolderID = fo.value("id").toString();
                m_SharedFolderId = m_FolderID;
                shareFolderWithConnectedDevices(m_FolderID);
                return;
            }
        }

        // 3) Not found: share it now
        shareLocalFolder(folderPath);
    };

    api->enqueueRequest(cfgReq);
}

void SyncthingManager::shareFolderWithConnectedDevices(const QString &folderId)
{
    //to remember share folder
    m_SharedFolderId = folderId;

    // Step 1: Get list of currently connected devices
    QUrl connUrl = api->m_baseUrl;
    connUrl.setPath(QString(CONNECTEDDEVICE));
    ApiRequest connReq;
    connReq.method = ApiRequest::GET;
    connReq.url    = connUrl;
    connReq.callback = [this, folderId](QNetworkReply *connReply) {
        if (connReply->error() != QNetworkReply::NoError) {
            emit globalError(
                        QString("Failed to fetch connections: %1")
                        .arg(connReply->errorString()));
            connReply->deleteLater();
            return;
        }
        // Parse connections JSON
        QJsonDocument connDoc = QJsonDocument::fromJson(connReply->readAll());
        connReply->deleteLater();
        if (!connDoc.isObject()) {
            emit globalError("Unexpected /rest/system/connections format");
            return;
        }
        QJsonObject allConns = connDoc.object().value("connections").toObject();
        QStringList connectedIds;
        for (auto it = allConns.begin(); it != allConns.end(); ++it) {
            QJsonObject c = it.value().toObject();
            if (c.value("connected").toBool()) {
                connectedIds << it.key();
            }
        }
        if (connectedIds.isEmpty()) {
            emit globalError("No devices currently connected");
            return;
        }

        // Step 2: Fetch full config
        QUrl cfgUrl = api->m_baseUrl;
        cfgUrl.setPath(QString(CONFIG));
        ApiRequest cfgReq;
        cfgReq.method = ApiRequest::GET;
        cfgReq.url    = cfgUrl;
        cfgReq.callback = [this, folderId, connectedIds, cfgUrl](QNetworkReply *cfgReply) {
            if (cfgReply->error() != QNetworkReply::NoError) {
                emit globalError(
                            QString("Failed to fetch config: %1")
                            .arg(cfgReply->errorString()));
                cfgReply->deleteLater();
                return;
            }
            QJsonParseError pe;
            QJsonDocument cfgDoc = QJsonDocument::fromJson(cfgReply->readAll(), &pe);
            cfgReply->deleteLater();
            if (pe.error != QJsonParseError::NoError || !cfgDoc.isObject()) {
                emit globalError(
                            QString("Invalid config JSON: %1")
                            .arg(pe.errorString()));
                return;
            }
            QJsonObject cfg = cfgDoc.object();

            // Locate and update the folder's devices array
            if (!cfg.contains("folders") || !cfg["folders"].isArray()) {
                emit globalError("Config missing \"folders\" array");
                return;
            }
            QJsonArray folders = cfg["folders"].toArray();
            bool found = false;
            for (int i = 0; i < folders.size(); ++i) {
                QJsonObject fo = folders.at(i).toObject();
                if (fo.value("id").toString() == folderId) {
                    // Build devices array
                    QJsonArray devArr;
                    for (const QString &devId : connectedIds) {
                        QJsonObject d;
                        d["deviceID"] = devId;
                        devArr.append(d);
                    }
                    fo["devices"] = devArr;
                    folders.replace(i, fo);
                    found = true;
                    break;
                }
            }
            if (!found) {
                emit globalError(
                            QString("Folder \"%1\" not found in config").arg(folderId));
                return;
            }
            cfg["folders"] = folders;

            // Step 3: POST updated config
            ApiRequest postReq;
            postReq.method  = ApiRequest::POST;
            postReq.url     = cfgUrl;
            postReq.payload = QJsonDocument(cfg).toJson(QJsonDocument::Compact);
            postReq.callback = [this, folderId](QNetworkReply *postReply) {
                if (postReply->error() == QNetworkReply::NoError) {
                    qDebug() << "[SyncthingManager] Shared folder"
                             << folderId << "to all connected devices.";
                    emit folderRescanned(folderId);
                } else {
                    emit globalError(
                                QString("Failed to share folder \"%1\": %2")
                                .arg(folderId, postReply->errorString()));
                }
                postReply->deleteLater();
            };
            api->enqueueRequest(postReq);
        };
        api->enqueueRequest(cfgReq);
    };
    api->enqueueRequest(connReq);
}




void SyncthingManager::addDeviceToSharedFolder(const QString &deviceId)
{
    // 1) GET the full config
    QUrl cfgUrl = api->m_baseUrl;
    cfgUrl.setPath(QString(CONFIG));  // "/rest/system/config"
    ApiRequest getReq;
    getReq.method = ApiRequest::GET;
    getReq.url    = cfgUrl;
    getReq.callback = [this, deviceId, cfgUrl](QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit globalError(
                        QString("addDeviceToSharedFolder: config fetch failed: %1")
                        .arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        // 2) Parse and locate our folder
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        if (!doc.isObject()) {
            emit globalError("addDeviceToSharedFolder: invalid config JSON");
            return;
        }
        QJsonObject cfg     = doc.object();
        QJsonArray  folders = cfg.value("folders").toArray();
        bool        found   = false;

        for (int i = 0; i < folders.size(); ++i) {
            QJsonObject fo = folders.at(i).toObject();
            if (fo.value("id").toString() == m_SharedFolderId) {
                found = true;

                // 3) Build or extend the "devices" array
                QJsonArray devArr = fo.value("devices").toArray();
                bool exists = false;
                for (auto dv : devArr) {
                    if (dv.toObject().value("deviceID").toString() == deviceId) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    QJsonObject newDev;
                    newDev["deviceID"] = deviceId;
                    devArr.append(newDev);
                    fo["devices"] = devArr;
                    folders.replace(i, fo);
                }
                break;
            }
        }

        if (!found) {
            emit globalError(
                        QString("addDeviceToSharedFolder: folder '%1' not in config")
                        .arg(m_SharedFolderId));
            return;
        }

        // 4) Write the updated config back
        cfg["folders"] = folders;
        QByteArray payload = QJsonDocument(cfg).toJson(QJsonDocument::Compact);
        ApiRequest postReq;
        postReq.method  = ApiRequest::POST;
        postReq.url     = cfgUrl;
        postReq.payload = payload;
        postReq.callback = [this, deviceId](QNetworkReply *postReply) {
            if (postReply->error() != QNetworkReply::NoError) {
                emit globalError(
                            QString("addDeviceToSharedFolder: failed to share to %1: %2")
                            .arg(deviceId, postReply->errorString()));
            } else {
                qDebug() << "[SyncthingManager] Shared folder"
                         << m_SharedFolderId
                         << "to new device" << deviceId;
            }
            postReply->deleteLater();
        };
        api->enqueueRequest(postReq);
    };
    api->enqueueRequest(getReq);
}



void SyncthingManager::onDeviceAdded(const QString &deviceId)
{
    // If we haven’t yet shared any folder, nothing to do.
    if (m_SharedFolderId.isEmpty())
        return;

    addDeviceToSharedFolder(deviceId);
}

