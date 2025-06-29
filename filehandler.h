#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include <QObject>
#include <QJsonObject>
#include <QString>

#define LOGPATH "/work/log/syncthing"
class FileHandler
{
public:
    explicit FileHandler() = delete;  // Make class static

    static bool writeJsonToFile(const QJsonObject& jsonObj);

private:
    static QString generateTimestampFilename();
};

#endif // FILEHANDLER_H
