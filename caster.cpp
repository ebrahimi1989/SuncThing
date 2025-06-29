#include "caster.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

QMap<QString, Device> Caster::parseDevices(const QJsonDocument &doc)
{
    QMap<QString, Device> devices;
    if (!doc.isArray())
        return devices;

    QJsonArray arr = doc.array();
    for (const QJsonValue &val : arr) {
        if (!val.isObject())
            continue;
        QJsonObject obj = val.toObject();
        Device dev;
        dev.id = obj.value("deviceID").toString();
        dev.devName = obj.value("name").toString();
        QJsonArray addrs = obj.value("addresses").toArray();
        for (const QJsonValue &a : addrs) {
            QString addr = a.toString();
            if (addr != "dynamic" && !addr.isEmpty()) {
                int idx = addr.lastIndexOf('/');
                dev.ip = (idx != -1 ? addr.mid(idx+1) : addr);
                break;
            }
        }
        if (!dev.id.isEmpty())
            devices.insert(dev.id, dev);
    }
    return devices;
}

QMap<QString, QSet<Folder>> Caster::parseFolders(const QJsonDocument &doc)
{
    QMap<QString, QSet<Folder>> map;
    if (!doc.isArray())
        return map;
    QJsonArray arr = doc.array();
    for (const QJsonValue &val : arr) {
        if (!val.isObject())
            continue;
        QJsonObject obj = val.toObject();
        Folder folder;
        folder.id = obj.value("id").toString();
        folder.label = obj.value("label").toString();
        // Items and lastUpdatedTime will be updated later.
        QJsonArray devArray = obj.value("devices").toArray();
        for (const QJsonValue &dval : devArray) {
            QJsonObject dObj = dval.toObject();
            QString devId = dObj.value("deviceID").toString();
            if (!devId.isEmpty()) {
                map[devId].insert(folder);
            }
        }
    }
    return map;
}
