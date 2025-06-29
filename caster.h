#ifndef CASTER_H
#define CASTER_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include "structbase.h"



// For QSet<Folder>, define equality and hash based on folder.id.
inline bool operator==(const Folder &f1, const Folder &f2) {
    return f1.id == f2.id;
}
inline uint qHash(const Folder &f, uint seed = 0) {
    return qHash(f.id, seed);
}

class Caster
{
public:
    // Parse devices JSON from /rest/config/devices (expects a JSON array)
    static QMap<QString, Device> parseDevices(const QJsonDocument &doc);

    // Parse folders JSON from /rest/config/folders (expects a JSON array)
    // Returns a mapping: key = device ID, value = set of Folder objects.
    static QMap<QString, QSet<Folder>> parseFolders(const QJsonDocument &doc);
};

#endif // SYNCTHINGCASTER_H
