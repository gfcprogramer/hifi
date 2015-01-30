//
//  IceServer.h
//  ice-server/src
//
//  Created by Stephen Birarda on 2014-10-01.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_IceServer_h
#define hifi_IceServer_h

#include <QtCore/QCoreApplication>
#include <QtCore/QSharedPointer>
#include <QUdpSocket>

#include <NetworkPeer.h>
#include <HTTPConnection.h>
#include <HTTPManager.h>

typedef QHash<QUuid, SharedNetworkPeer> NetworkPeerHash;

class IceServer : public QCoreApplication, public HTTPRequestHandler {
    Q_OBJECT
public:
    IceServer(int argc, char* argv[]);
    bool handleHTTPRequest(HTTPConnection* connection, const QUrl& url, bool skipSubHandler = false);
private slots:
    void processDatagrams();
    void clearInactivePeers();
private:
    
    void sendHeartbeatResponse(const HifiSockAddr& destinationSockAddr, QSet<QUuid>& connections);
    
    QUuid _id;
    QUdpSocket _serverSocket;
    NetworkPeerHash _activePeers;
    QHash<QUuid, QSet<QUuid> > _currentConnections;
    HTTPManager _httpManager;
};

#endif // hifi_IceServer_h