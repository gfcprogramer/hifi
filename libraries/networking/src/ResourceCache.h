//
//  ResourceCache.h
//  libraries/shared/src
//
//  Created by Andrzej Kapolka on 2/27/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ResourceCache_h
#define hifi_ResourceCache_h

#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QUrl>
#include <QWeakPointer>

class QNetworkReply;
class QTimer;

class Resource;

static const qint64 BYTES_PER_MEGABYTES = 1024 * 1024;
static const qint64 BYTES_PER_GIGABYTES = 1024 * BYTES_PER_MEGABYTES;

// Windows can have troubles allocating that much memory in ram sometimes
// so default cache size at 100 MB on windows (1GB otherwise)
#ifdef Q_OS_WIN32
static const qint64 DEFAULT_UNUSED_MAX_SIZE = 100 * BYTES_PER_MEGABYTES;
#else
static const qint64 DEFAULT_UNUSED_MAX_SIZE = 1024 * BYTES_PER_MEGABYTES;
#endif
static const qint64 MIN_UNUSED_MAX_SIZE = 0;
static const qint64 MAX_UNUSED_MAX_SIZE = 10 * BYTES_PER_GIGABYTES;

/// Base class for resource caches.
class ResourceCache : public QObject {
    Q_OBJECT
    
public:
    static void setRequestLimit(int limit) { _requestLimit = limit; }
    static int getRequestLimit() { return _requestLimit; }
    
    void setUnusedResourceCacheSize(qint64 unusedResourcesMaxSize);
    qint64 getUnusedResourceCacheSize() const { return _unusedResourcesMaxSize; }

    static const QList<Resource*>& getLoadingRequests() { return _loadingRequests; }

    static int getPendingRequestCount() { return _pendingRequests.size(); }

    ResourceCache(QObject* parent = NULL);
    virtual ~ResourceCache();

    void refresh(const QUrl& url);

protected:
    qint64 _unusedResourcesMaxSize = DEFAULT_UNUSED_MAX_SIZE;
    qint64 _unusedResourcesSize = 0;
    QMap<int, QSharedPointer<Resource> > _unusedResources;

    /// Loads a resource from the specified URL.
    /// \param fallback a fallback URL to load if the desired one is unavailable
    /// \param delayLoad if true, don't load the resource immediately; wait until load is first requested
    /// \param extra extra data to pass to the creator, if appropriate
    Q_INVOKABLE QSharedPointer<Resource> getResource(const QUrl& url, const QUrl& fallback = QUrl(),
        bool delayLoad = false, void* extra = NULL);

    /// Creates a new resource.
    virtual QSharedPointer<Resource> createResource(const QUrl& url,
        const QSharedPointer<Resource>& fallback, bool delayLoad, const void* extra) = 0;
    
    void addUnusedResource(const QSharedPointer<Resource>& resource);
    void removeUnusedResource(const QSharedPointer<Resource>& resource);
    void reserveUnusedResource(qint64 resourceSize);
    
    static void attemptRequest(Resource* resource);
    static void requestCompleted(Resource* resource);

private:
    friend class Resource;

    QHash<QUrl, QWeakPointer<Resource> > _resources;
    int _lastLRUKey = 0;
    
    static int _requestLimit;
    static QList<QPointer<Resource> > _pendingRequests;
    static QList<Resource*> _loadingRequests;
};

/// Base class for resources.
class Resource : public QObject {
    Q_OBJECT

public:
    
    Resource(const QUrl& url, bool delayLoad = false);
    ~Resource();
    
    /// Returns the key last used to identify this resource in the unused map.
    int getLRUKey() const { return _lruKey; }

    /// Makes sure that the resource has started loading.
    void ensureLoading();

    /// Sets the load priority for one owner.
    virtual void setLoadPriority(const QPointer<QObject>& owner, float priority);
    
    /// Sets a set of priorities at once.
    virtual void setLoadPriorities(const QHash<QPointer<QObject>, float>& priorities);
    
    /// Clears the load priority for one owner.
    virtual void clearLoadPriority(const QPointer<QObject>& owner);
    
    /// Returns the highest load priority across all owners.
    float getLoadPriority();

    /// Checks whether the resource has loaded.
    bool isLoaded() const { return _loaded; }

    /// For loading resources, returns the number of bytes received.
    qint64 getBytesReceived() const { return _bytesReceived; }
    
    /// For loading resources, returns the number of total bytes (<= zero if unknown).
    qint64 getBytesTotal() const { return _bytesTotal; }

    /// For loading resources, returns the load progress.
    float getProgress() const { return (_bytesTotal <= 0) ? 0.0f : (float)_bytesReceived / _bytesTotal; }

    /// Refreshes the resource.
    void refresh();

    void setSelf(const QWeakPointer<Resource>& self) { _self = self; }

    void setCache(ResourceCache* cache) { _cache = cache; }

    Q_INVOKABLE void allReferencesCleared();
    
    const QUrl& getURL() const { return _url; }

signals:

    /// Fired when the resource has been loaded.
    void loaded();

protected slots:

    void attemptRequest();

protected:

    virtual void init();

    /// Called when the download has finished.  The recipient should delete the reply when done with it.
    virtual void downloadFinished(QNetworkReply* reply) = 0;

    /// Should be called by subclasses when all the loading that will be done has been done.
    Q_INVOKABLE void finishedLoading(bool success);

    /// Reinserts this resource into the cache.
    virtual void reinsert();

    QUrl _url;
    QNetworkRequest _request;
    bool _startedLoading = false;
    bool _failedToLoad = false;
    bool _loaded = false;
    QHash<QPointer<QObject>, float> _loadPriorities;
    QWeakPointer<Resource> _self;
    QPointer<ResourceCache> _cache;
    
private slots:
    
    void handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void handleReplyError();
    void handleReplyFinished();
    void handleReplyTimeout();

private:
    
    void setLRUKey(int lruKey) { _lruKey = lruKey; }
    
    void makeRequest();
    
    void handleReplyError(QNetworkReply::NetworkError error, QDebug debug);
    
    friend class ResourceCache;
    
    int _lruKey = 0;
    QNetworkReply* _reply = nullptr;
    QTimer* _replyTimer = nullptr;
    qint64 _bytesReceived = 0;
    qint64 _bytesTotal = 0;
    int _attempts = 0;
};

uint qHash(const QPointer<QObject>& value, uint seed = 0);

#endif // hifi_ResourceCache_h
