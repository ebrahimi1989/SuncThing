 #ifndef CONFIGHANDLER_H
#define CONFIGHANDLER_H

#include <QString>
#include <QJsonObject>
#include <QXmlStreamReader>

#define BINARYCONFIGDIR "/.local/state/syncthing/config.xml"
#define SERVICECONFIG "/work/config/syncthing.conf"

class ConfigHandler
{
public:
    /// Constructs a ConfigHandler for the given configuration file.
    ConfigHandler();

    /// Loads the configuration file and parses out the base URL and API key.
    /// Returns true if parsing was successful.

    /// Returns the extracted base URL (for example, "127.0.0.1:8384").
    QString baseUrl() const;

    /// Returns the extracted API key.
    QString apiKey() const;


    void setLastEvent(int port);
    int getLastEvent() const;
    QString getUpdatterAddress() const;
    QString getSyncName() const;
    void setsyncName(QString name);
    bool getWrapperIsServer() const;
    void setWrapperIsServer(bool isServer);
private:
    bool loadBinaryConfig();
    QJsonObject xml2json(QXmlStreamReader &xml);

    QString m_baseUrl;
    QString m_apiKey;
    QJsonObject config;
    QString configPath;

    void createDefaultConfigFile();
};

#endif // CONFIGHANDLER_H
