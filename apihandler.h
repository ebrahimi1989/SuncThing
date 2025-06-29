#ifndef APIHANDLER_H
#define APIHANDLER_H

#include <QObject>
#include <QQueue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <mutex>
#include <QLoggingCategory>

// Declare a logging category.
Q_DECLARE_LOGGING_CATEGORY(SyncthingHandlerLog)

// Structure representing a Syncthing API request.
struct ApiRequest {
    enum HttpMethod { GET, POST, PATCH, PUT, DELETE_ } method;
    QUrl url;
    QByteArray payload;
    std::function<void(QNetworkReply*)> callback;
    int priority = 1;      // Default priority.
    int retryCount = 0;    // Times this request has been retried.
};

class ApiHandler : public QObject
{
    Q_OBJECT
public:
    static ApiHandler* getInstance(); // Singleton instance.
    ApiHandler(const ApiHandler&) = delete;
    ApiHandler& operator=(const ApiHandler&) = delete;
    ~ApiHandler();

    // Setters.
    void setApiKey(const QString &apiKey);
    void setBaseUrl(const QUrl &baseUrl);
    void setRetryCount(int maxRetries);
    void setQueueLimit(int limit);         // Limit for the queue size.
    void setRequestTimeout(int timeoutMs); // Timeout for individual requests.

    // Enqueue an API request.
    void enqueueRequest(const ApiRequest &req);

    // For testing or inspection.
    int getQueueSize() const;

    QUrl m_baseUrl;                     // Base URL for Syncthing requests.
signals:
    // Emitted when a request is processed.
    void requestProcessed(const QString &info);
    // Emitted on any API error.
    void globalError(const QString &errorMessage);
    // Emitted when the queue size changes.
    void queueSizeChanged(int size);

    void connectionError();

public slots:
    // Process the next request in the queue.
    void processNextRequest();
    // Timer event to check the queue.
    void onTimerTick();
    // Retry a failed request.
    void retryRequest(ApiRequest req);

private:
    explicit ApiHandler(QObject *parent = nullptr);
    void handleNetworkReply(QNetworkReply *reply, const ApiRequest &req);

    QQueue<ApiRequest> m_requestQueue; // Request queue.
    std::mutex m_queueMutex;            // Protects the queue.
    bool m_requestInProgress;           // Indicates if a request is in flight.
    QNetworkAccessManager *m_networkManager; // Used for network calls.
    QString m_apiKey;                   // API key.
    QTimer m_timer;                     // Timer for polling the queue.
    int m_maxRetries;                   // Maximum retries for a request.
    int m_maxQueueSize;                 // Maximum allowed queued requests.
    int m_pollingInterval;              // Milliseconds between processing attempts.
    int m_requestTimeoutMs;             // Timeout for individual requests.
};

#endif // APIHANDLER_H

