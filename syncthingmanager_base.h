#ifndef SYNCTHINGMANAGER_BASE_H
#define SYNCTHINGMANAGER_BASE_H

#include <QObject>
#include <memory>

class ISyncthingManager : public QObject
{
    Q_OBJECT
public:                       // --------  C‑Tors  --------
    virtual ~ISyncthingManager() = default;
    ISyncthingManager(const ISyncthingManager&)            = delete;
    ISyncthingManager& operator=(const ISyncthingManager&) = delete;

    // ---------- Factory access ----------
    static ISyncthingManager* instance();   // returns Client‑ or Server‑manager
    static void destroy();                  // call on clean shut‑down

    // ----‑‑‑ API that both concrete classes must expose  ----
    virtual void makeUpdate()                            = 0;   // node‑update
    virtual void getMyDeviceId()                         = 0;
    virtual void getSystemLog()                          = 0;
    virtual void startHealthCheck()                      = 0;

signals:                                                // forwarded from impls
    void updateAvailable();
    void updateDone();
    void globalError(const QString& err);

protected:
    explicit ISyncthingManager(QObject* parent = nullptr) : QObject(parent) {}
};

#endif // SYNCTHINGMANAGER_BASE_H
