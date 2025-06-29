#include "apihandler.h"
#include <QDebug>
#include <QNetworkRequest>
#include <QTimer>
#include <limits>

Q_LOGGING_CATEGORY(SyncthingHandlerLog, "syncthinghandler")

static ApiHandler* instance = nullptr;

ApiHandler::~ApiHandler() {}

ApiHandler::ApiHandler(QObject *parent)
    : QObject(parent),
      m_requestInProgress(false),
      m_networkManager(new QNetworkAccessManager(this)),
      m_maxRetries(1),          // Default: one retry.
      m_maxQueueSize(20),       // Default queue limit is 10.
      m_pollingInterval(200),   // Process queue every 100ms.
      m_requestTimeoutMs(100)  // Request timeout set to 1 second.
{
    // Start the timer to process requests.
    connect(&m_timer, &QTimer::timeout, this, &ApiHandler::onTimerTick);
    m_timer.start(m_pollingInterval);
}

ApiHandler* ApiHandler::getInstance()
{
    if (!instance)
        instance = new ApiHandler();
    return instance;
}

void ApiHandler::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
}

void ApiHandler::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

void ApiHandler::setRetryCount(int maxRetries)
{
    m_maxRetries = maxRetries;
    qDebug() << "Max retries set to" << m_maxRetries;
}

void ApiHandler::setQueueLimit(int limit)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_maxQueueSize = limit;
    qDebug() << "Max queue size set to" << m_maxQueueSize;
    // Remove excess requests if current queue exceeds the new limit.
    while (m_requestQueue.size() > m_maxQueueSize) {
        int lowestPriority = std::numeric_limits<int>::max();
        int removeIndex = -1;
        for (int i = 0; i < m_requestQueue.size(); ++i) {
            if (m_requestQueue.at(i).priority < lowestPriority) {
                lowestPriority = m_requestQueue.at(i).priority;
                removeIndex = i;
            }
        }
        if (removeIndex != -1) {
            ApiRequest removed = m_requestQueue.takeAt(removeIndex);
            qDebug() << "Removed request due to queue limit:" << removed.url.toString()
                     << "with priority:" << removed.priority;
            emit globalError(QString("Removed request %1 due to queue limit")
                             .arg(removed.url.toString()));
            emit queueSizeChanged(m_requestQueue.size());
        }
    }
}

void ApiHandler::setRequestTimeout(int timeoutMs)
{
    m_requestTimeoutMs = timeoutMs;
    qDebug() << "Request timeout set to" << m_requestTimeoutMs << "ms";
}

void ApiHandler::enqueueRequest(const ApiRequest &req)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    // Enforce queue size limit: if queue is full, remove the lowest-priority request.
    if (m_requestQueue.size() >= m_maxQueueSize) {
        int lowestPriority = std::numeric_limits<int>::max();
        int removeIndex = -1;
        for (int i = 0; i < m_requestQueue.size(); ++i) {
            if (m_requestQueue.at(i).priority < lowestPriority) {
                lowestPriority = m_requestQueue.at(i).priority;
                removeIndex = i;
            }
        }
        if (removeIndex != -1) {
            ApiRequest removed = m_requestQueue.takeAt(removeIndex);
            qDebug() << "Removed low priority request:" << removed.url.toString()
                     << "with priority:" << removed.priority;
            emit globalError(QString("Removed request %1 due to low priority")
                             .arg(removed.url.toString()));
            emit queueSizeChanged(m_requestQueue.size());
        }
    }
    m_requestQueue.enqueue(req);
    emit queueSizeChanged(m_requestQueue.size());
    qDebug() << "Request enqueued. New queue size:" << m_requestQueue.size();
}

int ApiHandler::getQueueSize() const {
    return m_requestQueue.size();
}

void ApiHandler::processNextRequest()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_requestInProgress || m_requestQueue.isEmpty())
        return;

    m_requestInProgress = true;
    ApiRequest req = m_requestQueue.dequeue();
    emit queueSizeChanged(m_requestQueue.size());

    QNetworkRequest netReq(req.url);
    if (!m_apiKey.isEmpty())
        netReq.setRawHeader("X-API-Key", m_apiKey.toUtf8());

    QNetworkReply *reply = nullptr;
    switch (req.method) {
        case ApiRequest::GET:
            reply = m_networkManager->get(netReq);
            break;
        case ApiRequest::POST:
            netReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            reply = m_networkManager->post(netReq, req.payload);
            break;
        case ApiRequest::PATCH:
            netReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            reply = m_networkManager->sendCustomRequest(netReq, "PATCH", req.payload);
            break;
        case ApiRequest::PUT:
            netReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            reply = m_networkManager->put(netReq, req.payload);
            break;
        case ApiRequest::DELETE_:
            reply = m_networkManager->deleteResource(netReq);
            break;
        default:
            reply = m_networkManager->get(netReq);
    }

    // Set up a timer to abort the request if it exceeds the timeout limit.
    QTimer *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, reply, [reply, this, req]() {
        if (reply->isRunning()) {
            reply->abort();
            qWarning() << "Request timed out:" << req.url;
            emit globalError(QString("Request timed out: %1").arg(req.url.toString()));
        }
    });
    timeoutTimer->start(m_requestTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, req, timeoutTimer]() {
        timeoutTimer->stop();
        handleNetworkReply(reply, req);
        timeoutTimer->deleteLater();
    });
}

void ApiHandler::handleNetworkReply(QNetworkReply *reply, const ApiRequest &req)
{
    if (reply->error() != QNetworkReply::NoError) {
        if(reply->error() == QNetworkReply::ConnectionRefusedError){
        emit connectionError();
            }
        qWarning() << "Syncthing request failed to" << req.url << ":" << reply->errorString();

        retryRequest(req);
    }  else {
        if (req.callback)
            req.callback(reply);
        emit requestProcessed(QString("Request to %1 processed successfully").arg(req.url.toString()));
    }
    reply->deleteLater();
    m_requestInProgress = false;
}

void ApiHandler::retryRequest(ApiRequest req)
{
    if (req.retryCount < m_maxRetries) {
        req.retryCount++;
        qWarning() << "Retrying request to" << req.url << "(Attempt" << req.retryCount << ")";
        enqueueRequest(req);
    } else {
        qCritical() << "Max retries reached for" << req.url;
        emit globalError(QString("Max retries reached for request to %1").arg(req.url.toString()));
    }
}

void ApiHandler::onTimerTick()
{
    processNextRequest();
}
