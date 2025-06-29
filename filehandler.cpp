#include "filehandler.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDebug>

bool FileHandler::writeJsonToFile(const QJsonObject& jsonObj)
{
    // Create directory if it doesn't exist
    QDir dir;
    if (!dir.mkpath(LOGPATH)) {
        qWarning() << "Failed to create directory path:" << QString(LOGPATH);
        return false;
    }

    // Generate full file path with timestamp
    const QString fullPath = QDir(QString(LOGPATH)).filePath(generateTimestampFilename());

    // Use QSaveFile for atomic write operation
    QSaveFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly )) {
        qWarning() << "Failed to open file for writing:" << fullPath
                   << "Error:" << file.errorString();
        return false;
    }

    // Convert JSON object to text format
    const QJsonDocument doc(jsonObj);
    const QByteArray jsonData = doc.toJson(QJsonDocument::Indented);

    // Write to file
    const qint64 bytesWritten = file.write(jsonData);
    if (bytesWritten == -1) {
        qWarning() << "Failed to write to file:" << fullPath
                   << "Error:" << file.errorString();
        file.cancelWriting();
        return false;
    }

    // Finalize the write operation
    if (!file.commit()) {
        qWarning() << "Failed to commit file changes:" << fullPath
                   << "Error:" << file.errorString();
        return false;
    }

    return true;
}

QString FileHandler::generateTimestampFilename()
{
    const QDateTime now = QDateTime::currentDateTime();
    return now.toString("yyyyMMdd_HHmmss") + ".log";
}
