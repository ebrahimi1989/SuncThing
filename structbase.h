#ifndef STRUCTBASE_H
#define STRUCTBASE_H
#include <QString>
#include <QDateTime>
#include <QSet>

// Domain structures


struct Folder {
    QSet<QString> items;       // Set of item names in the folder
    QDateTime lastUpdatedTime; // Last modification/sync time
    QString id;                // Unique folder identifier
    QString label;             // Folder label or name
    QString path;             // Optional: proposed destination folder (if provided)
    bool folderIsOffer = false;
};
struct Device {
    QString devName;   // Device name
    QString ip;        // IP address
    QString id;        // Unique device identifier
    QSet <Folder>folders;
};

#endif // STRUCTBASE_H
