#include "configHandler.h"
#include <QFile>
#include <QXmlStreamReader>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

ConfigHandler::ConfigHandler()
{
    QString homeDir = QDir::homePath();

    configPath = homeDir+QString(BINARYCONFIGDIR);

    bool binaryLoaded = loadBinaryConfig();
    if(binaryLoaded){
        qDebug()<<"Binary Config loaded sucsessfully";
    }else{
        qWarning()<<"cant find Binary config.xml";
    }

      createDefaultConfigFile(); // Ensure the config file exists
}

bool ConfigHandler::loadBinaryConfig()
{

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open file";
        return false;
    }

    QXmlStreamReader xml(&file);
    if (xml.readNextStartElement()) {
        QString rootName = xml.name().toString();
        QJsonObject content = xml2json(xml);
        config.insert(rootName, content);
    }
    QJsonDocument jsonDoc(config);
//    qDebug().noquote() << jsonDoc.toJson(QJsonDocument::Indented);
    QJsonObject configuration = config.value("configuration").toObject();

    // Get the "gui" object.
    if (!configuration.contains("gui")) {
        qWarning() << "No gui element found in configuration.";
        return false;
    }
    QJsonObject gui = configuration.value("gui").toObject();

    // Extract the API key.
    QString apikey;
    if (gui.contains("apikey")) {
        QJsonValue apiKeyValue = gui.value("apikey");
        if (apiKeyValue.isObject()) {
            QJsonObject apiObj = apiKeyValue.toObject();
            // First, check if it's stored in an attribute.
            if (apiObj.contains("@attributes")) {
                QJsonObject attrs = apiObj.value("@attributes").toObject();
                apikey = attrs.value("value").toString();
            } else {
                // Otherwise, check for text.
                apikey = apiObj.value("#text").toString();
            }
        } else if (apiKeyValue.isString()) {
            apikey = apiKeyValue.toString();
        }
    }

    // Extract the GUI address.
    QString address;
    if (gui.contains("address")) {
        QJsonValue addressValue = gui.value("address");
        if (addressValue.isObject())
            address = addressValue.toObject().value("#text").toString();
        else if (addressValue.isString())
            address = addressValue.toString();
    }

    qDebug() << "API Key:" << apikey;
    qDebug() << "GUI Address:" << address;

    m_apiKey= apikey;
    m_baseUrl = QString("http://"+address);
    return true;
}

QString ConfigHandler::baseUrl() const
{
    return m_baseUrl;
}

QString ConfigHandler::apiKey() const
{
    return m_apiKey;
}

QJsonObject ConfigHandler::xml2json(QXmlStreamReader &xml)
{
    QJsonObject jsonObj;

    // Process attributes if any
    if (xml.attributes().size() > 0) {
        QJsonObject attrObj;
        for (const QXmlStreamAttribute &attr : xml.attributes()) {
            attrObj.insert(attr.name().toString(), attr.value().toString());
        }
        // Store attributes under a special key
        jsonObj.insert("@attributes", attrObj);
    }

    // Process child elements and text nodes
    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            QString elementName = xml.name().toString();
            // Recursively process the child element
            QJsonObject childObj = xml2json(xml);

            // If the key already exists, convert it to an array or append to the existing array
            if (jsonObj.contains(elementName)) {
                QJsonValue existingValue = jsonObj.value(elementName);
                QJsonArray arr;
                if (existingValue.isArray()) {
                    arr = existingValue.toArray();
                } else {
                    arr.append(existingValue);
                }
                arr.append(childObj);
                jsonObj.insert(elementName, arr);
            } else {
                jsonObj.insert(elementName, childObj);
            }
        } else if (xml.isCharacters() && !xml.isWhitespace()) {
            // Store text content under a special key
            QString text = xml.text().toString();
            jsonObj.insert("#text", text);
        } else if (xml.isEndElement()) {
            break;
        }
    }
    return jsonObj;
}




// Creates the default config file if it doesn't exist
void ConfigHandler::createDefaultConfigFile() {
    QFile configFile(SERVICECONFIG);
    if (!configFile.exists()) {
        if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&configFile);
            out << "[Syncthing]\n";
            out << "IP=192.168.1.101:22000\n";
            out << "Event=0\n";
            out << "Name=Wrapper\n";
            out << "IsServer=false";
            configFile.close();
            qDebug() << "Default config file created at:" << SERVICECONFIG;
        } else {
            qWarning() << "Failed to create the config file at:" << SERVICECONFIG;
        }
    } else {
        qDebug() << "Config file already exists at:" << SERVICECONFIG;
    }
}

// Reads the IP address from the config file
QString ConfigHandler::getUpdatterAddress() const {
    QSettings settings(SERVICECONFIG, QSettings::IniFormat);
    //ip and port
    return settings.value("Syncthing/IP", "192.168.1.101:22000").toString(); // Default to 192.168.1.101:22000 if not found
}

// Reads the LastEvent number from the config file
int ConfigHandler::getLastEvent() const {
    QSettings settings(SERVICECONFIG, QSettings::IniFormat);
    return settings.value("Syncthing/Event", 80).toInt(); // Default to 80 if not found
}

// Writes an integer (LastEvent) to the config file
void ConfigHandler::setLastEvent(int port) {
    QSettings settings(SERVICECONFIG, QSettings::IniFormat);
    settings.setValue("Syncthing/Event", port);
    qDebug() << "Event updated to:" << port;
}

//get SyncThing Device Name
QString ConfigHandler::getSyncName() const {
    QSettings settings(SERVICECONFIG, QSettings::IniFormat);
    return settings.value("Syncthing/Name", "Wrapper").toString(); // Default to Wrapper if not found
}

// Writes an QString SyncThing Device Name
void ConfigHandler::setsyncName(QString name) {
    QSettings settings(SERVICECONFIG, QSettings::IniFormat);
    settings.setValue("Syncthing/Name", name);
    qDebug() << "Event updated to:" << name;
}

//get SyncThing Device Name
bool ConfigHandler::getWrapperIsServer() const {
    QSettings settings(SERVICECONFIG, QSettings::IniFormat);
    return settings.value("Syncthing/IsServer", "false").toBool(); // Default to Wrapper if not found
}

// Writes an QString SyncThing Device Name
void ConfigHandler::setWrapperIsServer(bool isServer) {
    QSettings settings(SERVICECONFIG, QSettings::IniFormat);
    settings.setValue("Syncthing/IsServer", isServer);
    qDebug() << "Event updated to:" << isServer;
}
