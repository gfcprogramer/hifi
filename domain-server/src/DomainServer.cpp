//
//  DomainServer.cpp
//  domain-server/src
//
//  Created by Stephen Birarda on 9/26/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QSharedMemory>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

#include <AccountManager.h>
#include <HifiConfigVariantMap.h>
#include <HTTPConnection.h>
#include <LogUtils.h>
#include <PacketHeaders.h>
#include <Settings.h>
#include <SharedUtil.h>
#include <ShutdownEventListener.h>
#include <UUID.h>

#include "DomainServerNodeData.h"

#include "DomainServer.h"

int const DomainServer::EXIT_CODE_REBOOT = 234923;

const QString ICE_SERVER_DEFAULT_HOSTNAME = "ice.highfidelity.io";

DomainServer::DomainServer(int argc, char* argv[]) :
    QCoreApplication(argc, argv),
    _httpManager(DOMAIN_SERVER_HTTP_PORT, QString("%1/resources/web/").arg(QCoreApplication::applicationDirPath()), this),
    _httpsManager(NULL),
    _allAssignments(),
    _unfulfilledAssignments(),
    _pendingAssignedNodes(),
    _isUsingDTLS(false),
    _oauthProviderURL(),
    _oauthClientID(),
    _hostname(),
    _webAuthenticationStateSet(),
    _cookieSessionHash(),
    _automaticNetworkingSetting(),
    _settingsManager(),
    _iceServerSocket(ICE_SERVER_DEFAULT_HOSTNAME, ICE_SERVER_DEFAULT_PORT)
{
    LogUtils::init();

    setOrganizationName("High Fidelity");
    setOrganizationDomain("highfidelity.io");
    setApplicationName("domain-server");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    
    // make sure we have a fresh AccountManager instance
    // (need this since domain-server can restart itself and maintain static variables)
    AccountManager::getInstance(true);
    
    _settingsManager.setupConfigMap(arguments());
    
    // setup a shutdown event listener to handle SIGTERM or WM_CLOSE for us
#ifdef _WIN32
    installNativeEventFilter(&ShutdownEventListener::getInstance());
#else
    ShutdownEventListener::getInstance();
#endif
    
    qRegisterMetaType<DomainServerWebSessionData>("DomainServerWebSessionData");
    qRegisterMetaTypeStreamOperators<DomainServerWebSessionData>("DomainServerWebSessionData");
    
    if (optionallyReadX509KeyAndCertificate() && optionallySetupOAuth() && optionallySetupAssignmentPayment()) {
        // we either read a certificate and private key or were not passed one
        // and completed login or did not need to

        qDebug() << "Setting up LimitedNodeList and assignments.";
        setupNodeListAndAssignments();
        
        loadExistingSessionsFromSettings();
        
        // setup automatic networking settings with data server
        setupAutomaticNetworking();
        
        // preload some user public keys so they can connect on first request
        preloadAllowedUserPublicKeys();
    }
}

void DomainServer::restart() {
    qDebug() << "domain-server is restarting.";
    
    exit(DomainServer::EXIT_CODE_REBOOT);
}

bool DomainServer::optionallyReadX509KeyAndCertificate() {
    const QString X509_CERTIFICATE_OPTION = "cert";
    const QString X509_PRIVATE_KEY_OPTION = "key";
    const QString X509_KEY_PASSPHRASE_ENV = "DOMAIN_SERVER_KEY_PASSPHRASE";

    QString certPath = _settingsManager.getSettingsMap().value(X509_CERTIFICATE_OPTION).toString();
    QString keyPath = _settingsManager.getSettingsMap().value(X509_PRIVATE_KEY_OPTION).toString();

    if (!certPath.isEmpty() && !keyPath.isEmpty()) {
        // the user wants to use DTLS to encrypt communication with nodes
        // let's make sure we can load the key and certificate
//        _x509Credentials = new gnutls_certificate_credentials_t;
//        gnutls_certificate_allocate_credentials(_x509Credentials);

        QString keyPassphraseString = QProcessEnvironment::systemEnvironment().value(X509_KEY_PASSPHRASE_ENV);

        qDebug() << "Reading certificate file at" << certPath << "for DTLS.";
        qDebug() << "Reading key file at" << keyPath << "for DTLS.";

//        int gnutlsReturn = gnutls_certificate_set_x509_key_file2(*_x509Credentials,
//                                                                 certPath.toLocal8Bit().constData(),
//                                                                 keyPath.toLocal8Bit().constData(),
//                                                                 GNUTLS_X509_FMT_PEM,
//                                                                 keyPassphraseString.toLocal8Bit().constData(),
//                                                                 0);
//
//        if (gnutlsReturn < 0) {
//            qDebug() << "Unable to load certificate or key file." << "Error" << gnutlsReturn << "- domain-server will now quit.";
//            QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
//            return false;
//        }

//        qDebug() << "Successfully read certificate and private key.";

        // we need to also pass this certificate and private key to the HTTPS manager
        // this is used for Oauth callbacks when authorizing users against a data server

        QFile certFile(certPath);
        certFile.open(QIODevice::ReadOnly);

        QFile keyFile(keyPath);
        keyFile.open(QIODevice::ReadOnly);

        QSslCertificate sslCertificate(&certFile);
        QSslKey privateKey(&keyFile, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, keyPassphraseString.toUtf8());

        _httpsManager = new HTTPSManager(DOMAIN_SERVER_HTTPS_PORT, sslCertificate, privateKey, QString(), this, this);

        qDebug() << "TCP server listening for HTTPS connections on" << DOMAIN_SERVER_HTTPS_PORT;

    } else if (!certPath.isEmpty() || !keyPath.isEmpty()) {
        qDebug() << "Missing certificate or private key. domain-server will now quit.";
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
        return false;
    }

    return true;
}

bool DomainServer::optionallySetupOAuth() {
    const QString OAUTH_PROVIDER_URL_OPTION = "oauth-provider";
    const QString OAUTH_CLIENT_ID_OPTION = "oauth-client-id";
    const QString OAUTH_CLIENT_SECRET_ENV = "DOMAIN_SERVER_CLIENT_SECRET";
    const QString REDIRECT_HOSTNAME_OPTION = "hostname";

    const QVariantMap& settingsMap = _settingsManager.getSettingsMap();
    _oauthProviderURL = QUrl(settingsMap.value(OAUTH_PROVIDER_URL_OPTION).toString());
    
    // if we don't have an oauth provider URL then we default to the default node auth url
    if (_oauthProviderURL.isEmpty()) {
        _oauthProviderURL = DEFAULT_NODE_AUTH_URL;
    }
    
    AccountManager& accountManager = AccountManager::getInstance();
    accountManager.disableSettingsFilePersistence();
    accountManager.setAuthURL(_oauthProviderURL);
    
    _oauthClientID = settingsMap.value(OAUTH_CLIENT_ID_OPTION).toString();
    _oauthClientSecret = QProcessEnvironment::systemEnvironment().value(OAUTH_CLIENT_SECRET_ENV);
    _hostname = settingsMap.value(REDIRECT_HOSTNAME_OPTION).toString();

    if (!_oauthClientID.isEmpty()) {
        if (_oauthProviderURL.isEmpty()
            || _hostname.isEmpty()
            || _oauthClientID.isEmpty()
            || _oauthClientSecret.isEmpty()) {
            qDebug() << "Missing OAuth provider URL, hostname, client ID, or client secret. domain-server will now quit.";
            QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
            return false;
        } else {
            qDebug() << "OAuth will be used to identify clients using provider at" << _oauthProviderURL.toString();
            qDebug() << "OAuth Client ID is" << _oauthClientID;
        }
    }

    return true;
}

const QString DOMAIN_CONFIG_ID_KEY = "id";

const QString METAVERSE_AUTOMATIC_NETWORKING_KEY_PATH = "metaverse.automatic_networking";
const QString FULL_AUTOMATIC_NETWORKING_VALUE = "full";
const QString IP_ONLY_AUTOMATIC_NETWORKING_VALUE = "ip";
const QString DISABLED_AUTOMATIC_NETWORKING_VALUE = "disabled";

void DomainServer::setupNodeListAndAssignments(const QUuid& sessionUUID) {

    const QString CUSTOM_LOCAL_PORT_OPTION = "metaverse.local_port";

    QVariant localPortValue = _settingsManager.valueOrDefaultValueForKeyPath(CUSTOM_LOCAL_PORT_OPTION);
    unsigned short domainServerPort = (unsigned short) localPortValue.toUInt();
    
    QVariantMap& settingsMap = _settingsManager.getSettingsMap();

    unsigned short domainServerDTLSPort = 0;

    if (_isUsingDTLS) {
        domainServerDTLSPort = DEFAULT_DOMAIN_SERVER_DTLS_PORT;

        const QString CUSTOM_DTLS_PORT_OPTION = "dtls-port";

        if (settingsMap.contains(CUSTOM_DTLS_PORT_OPTION)) {
            domainServerDTLSPort = (unsigned short) settingsMap.value(CUSTOM_DTLS_PORT_OPTION).toUInt();
        }
    }

    QSet<Assignment::Type> parsedTypes;
    parseAssignmentConfigs(parsedTypes);

    populateDefaultStaticAssignmentsExcludingTypes(parsedTypes);
    
    // check for scripts the user wants to persist from their domain-server config
    populateStaticScriptedAssignmentsFromSettings();

    auto nodeList = DependencyManager::set<LimitedNodeList>(domainServerPort, domainServerDTLSPort);
    
    // no matter the local port, save it to shared mem so that local assignment clients can ask what it is
    QSharedMemory* sharedPortMem = new QSharedMemory(DOMAIN_SERVER_LOCAL_PORT_SMEM_KEY, this);
    quint16 localPort = nodeList->getNodeSocket().localPort();
    
    // attempt to create the shared memory segment
    if (sharedPortMem->create(sizeof(localPort)) || sharedPortMem->attach()) {
        sharedPortMem->lock();
        memcpy(sharedPortMem->data(), &localPort, sizeof(localPort));
        sharedPortMem->unlock();
        
        qDebug() << "Wrote local listening port" << localPort << "to shared memory at key" << DOMAIN_SERVER_LOCAL_PORT_SMEM_KEY;
    } else {
        qWarning() << "Failed to create and attach to shared memory to share local port with assignment-client children.";
    }
    
    // set our LimitedNodeList UUID to match the UUID from our config
    // nodes will currently use this to add resources to data-web that relate to our domain
    const QString METAVERSE_DOMAIN_ID_KEY_PATH = "metaverse.id";
    const QVariant* idValueVariant = valueForKeyPath(settingsMap, METAVERSE_DOMAIN_ID_KEY_PATH);
    if (idValueVariant) {
        nodeList->setSessionUUID(idValueVariant->toString());
    }

    connect(nodeList.data(), &LimitedNodeList::nodeAdded, this, &DomainServer::nodeAdded);
    connect(nodeList.data(), &LimitedNodeList::nodeKilled, this, &DomainServer::nodeKilled);

    QTimer* silentNodeTimer = new QTimer(this);
    connect(silentNodeTimer, SIGNAL(timeout()), nodeList.data(), SLOT(removeSilentNodes()));
    silentNodeTimer->start(NODE_SILENCE_THRESHOLD_MSECS);

    connect(&nodeList->getNodeSocket(), SIGNAL(readyRead()), SLOT(readAvailableDatagrams()));

    // add whatever static assignments that have been parsed to the queue
    addStaticAssignmentsToQueue();
}

bool DomainServer::didSetupAccountManagerWithAccessToken() {
    AccountManager& accountManager = AccountManager::getInstance();
    
    if (accountManager.hasValidAccessToken()) {
        // we already gave the account manager a valid access token
        return true;
    }
    
    if (!_oauthProviderURL.isEmpty()) {
        // check for an access-token in our settings, can optionally be overidden by env value
        const QString ACCESS_TOKEN_KEY_PATH = "metaverse.access_token";
        const QString ENV_ACCESS_TOKEN_KEY = "DOMAIN_SERVER_ACCESS_TOKEN";
        
        QString accessToken = QProcessEnvironment::systemEnvironment().value(ENV_ACCESS_TOKEN_KEY);
        
        if (accessToken.isEmpty()) {
            const QVariant* accessTokenVariant = valueForKeyPath(_settingsManager.getSettingsMap(), ACCESS_TOKEN_KEY_PATH);
            
            if (accessTokenVariant && accessTokenVariant->canConvert(QMetaType::QString)) {
                accessToken = accessTokenVariant->toString();
            } else {
                qDebug() << "A domain-server feature that requires authentication is enabled but no access token is present."
                    << "Set an access token via the web interface, in your user or master config"
                    << "at keypath metaverse.access_token or in your ENV at key DOMAIN_SERVER_ACCESS_TOKEN";
                return false;
            }
        }
        
        // give this access token to the AccountManager
        accountManager.setAccessTokenForCurrentAuthURL(accessToken);
        
        return true;

    } else {
        qDebug() << "Missing OAuth provider URL, but a domain-server feature was required that requires authentication." <<
            "domain-server will now quit.";
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
        
        return false;
    }
}

bool DomainServer::optionallySetupAssignmentPayment() {
    const QString PAY_FOR_ASSIGNMENTS_OPTION = "pay-for-assignments";
    const QVariantMap& settingsMap = _settingsManager.getSettingsMap();
    
    if (settingsMap.contains(PAY_FOR_ASSIGNMENTS_OPTION) &&
        settingsMap.value(PAY_FOR_ASSIGNMENTS_OPTION).toBool() &&
        didSetupAccountManagerWithAccessToken()) {
        
        qDebug() << "Assignments will be paid for via" << qPrintable(_oauthProviderURL.toString());
        
        // assume that the fact we are authing against HF data server means we will pay for assignments
        // setup a timer to send transactions to pay assigned nodes every 30 seconds
        QTimer* creditSetupTimer = new QTimer(this);
        connect(creditSetupTimer, &QTimer::timeout, this, &DomainServer::setupPendingAssignmentCredits);
        
        const qint64 CREDIT_CHECK_INTERVAL_MSECS = 5 * 1000;
        creditSetupTimer->start(CREDIT_CHECK_INTERVAL_MSECS);
        
        QTimer* nodePaymentTimer = new QTimer(this);
        connect(nodePaymentTimer, &QTimer::timeout, this, &DomainServer::sendPendingTransactionsToServer);
        
        const qint64 TRANSACTION_SEND_INTERVAL_MSECS = 30 * 1000;
        nodePaymentTimer->start(TRANSACTION_SEND_INTERVAL_MSECS);
    }

    return true;
}

void DomainServer::setupAutomaticNetworking() {
    auto nodeList = DependencyManager::get<LimitedNodeList>();
    
    const int STUN_REFLEXIVE_KEEPALIVE_INTERVAL_MSECS = 10 * 1000;
    const int STUN_IP_ADDRESS_CHECK_INTERVAL_MSECS = 30 * 1000;

    // setup our timer to check our IP via stun every X seconds
    QTimer* dynamicIPTimer = new QTimer(this);
    connect(dynamicIPTimer, &QTimer::timeout, this, &DomainServer::requestCurrentPublicSocketViaSTUN);
    
    _automaticNetworkingSetting =
        _settingsManager.valueOrDefaultValueForKeyPath(METAVERSE_AUTOMATIC_NETWORKING_KEY_PATH).toString();
    
    if (_automaticNetworkingSetting == FULL_AUTOMATIC_NETWORKING_VALUE) {
        dynamicIPTimer->start(STUN_REFLEXIVE_KEEPALIVE_INTERVAL_MSECS);
        
        // setup a timer to heartbeat with the ice-server every so often
        QTimer* iceHeartbeatTimer = new QTimer(this);
        connect(iceHeartbeatTimer, &QTimer::timeout, this, &DomainServer::performICEUpdates);
        iceHeartbeatTimer->start(ICE_HEARBEAT_INTERVAL_MSECS);
        
        // call our sendHeartbeatToIceServer immediately anytime a local or public socket changes
        connect(nodeList.data(), &LimitedNodeList::localSockAddrChanged,
                this, &DomainServer::sendHeartbeatToIceServer);
        connect(nodeList.data(), &LimitedNodeList::publicSockAddrChanged,
                this, &DomainServer::sendHeartbeatToIceServer);
        
        // attempt to update our public socket now, this will send a heartbeat once we get public socket
        requestCurrentPublicSocketViaSTUN();
        
        // in case the STUN lookup is still happening we should re-request a public socket once we get that address
        connect(&nodeList->getSTUNSockAddr(), &HifiSockAddr::lookupCompleted,
                this, &DomainServer::requestCurrentPublicSocketViaSTUN);
        
    }
    
    if (!didSetupAccountManagerWithAccessToken()) {
        qDebug() << "Cannot send heartbeat to data server without an access token.";
        qDebug() << "Add an access token to your config file or via the web interface.";
        
        return;
    }
    
    if (_automaticNetworkingSetting == IP_ONLY_AUTOMATIC_NETWORKING_VALUE ||
        _automaticNetworkingSetting == FULL_AUTOMATIC_NETWORKING_VALUE) {
        
        const QUuid& domainID = nodeList->getSessionUUID();
        
        if (!domainID.isNull()) {
            qDebug() << "domain-server" << _automaticNetworkingSetting << "automatic networking enabled for ID"
                << uuidStringWithoutCurlyBraces(domainID) << "via" << _oauthProviderURL.toString();
            
            if (_automaticNetworkingSetting == IP_ONLY_AUTOMATIC_NETWORKING_VALUE) {
                dynamicIPTimer->start(STUN_IP_ADDRESS_CHECK_INTERVAL_MSECS);
                
                // send public socket changes to the data server so nodes can find us at our new IP
                connect(nodeList.data(), &LimitedNodeList::publicSockAddrChanged,
                        this, &DomainServer::performIPAddressUpdate);
                
                // attempt to update our sockets now
                requestCurrentPublicSocketViaSTUN();
            } else {
                // send our heartbeat to data server so it knows what our network settings are
                sendHeartbeatToDataServer();
            }
        } else {
            qDebug() << "Cannot enable domain-server automatic networking without a domain ID."
            << "Please add an ID to your config file or via the web interface.";
            
            return;
        }
    } else {
        sendHeartbeatToDataServer();
    }
    
    qDebug() << "Updating automatic networking setting in domain-server to" << _automaticNetworkingSetting;
    
    // no matter the auto networking settings we should heartbeat to the data-server every 15s
    const int DOMAIN_SERVER_DATA_WEB_HEARTBEAT_MSECS = 15 * 1000;
    
    QTimer* dataHeartbeatTimer = new QTimer(this);
    connect(dataHeartbeatTimer, SIGNAL(timeout()), this, SLOT(sendHeartbeatToDataServer()));
    dataHeartbeatTimer->start(DOMAIN_SERVER_DATA_WEB_HEARTBEAT_MSECS);
}

void DomainServer::loginFailed() {
    qDebug() << "Login to data server has failed. domain-server will now quit";
    QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
}

void DomainServer::parseAssignmentConfigs(QSet<Assignment::Type>& excludedTypes) {
    // check for configs from the command line, these take precedence
    const QString ASSIGNMENT_CONFIG_REGEX_STRING = "config-([\\d]+)";
    QRegExp assignmentConfigRegex(ASSIGNMENT_CONFIG_REGEX_STRING);
    
    const QVariantMap& settingsMap = _settingsManager.getSettingsMap();

    // scan for assignment config keys
    QStringList variantMapKeys = settingsMap.keys();
    int configIndex = variantMapKeys.indexOf(assignmentConfigRegex);

    while (configIndex != -1) {
        // figure out which assignment type this matches
        Assignment::Type assignmentType = (Assignment::Type) assignmentConfigRegex.cap(1).toInt();

        if (assignmentType < Assignment::AllTypes && !excludedTypes.contains(assignmentType)) {
            QVariant mapValue = settingsMap[variantMapKeys[configIndex]];
            QVariantList assignmentList = mapValue.toList();

            if (assignmentType != Assignment::AgentType) {
                createStaticAssignmentsForType(assignmentType, assignmentList);
            }

            excludedTypes.insert(assignmentType);
        }

        configIndex = variantMapKeys.indexOf(assignmentConfigRegex, configIndex + 1);
    }
}

void DomainServer::addStaticAssignmentToAssignmentHash(Assignment* newAssignment) {
    qDebug() << "Inserting assignment" << *newAssignment << "to static assignment hash.";
    newAssignment->setIsStatic(true);
    _allAssignments.insert(newAssignment->getUUID(), SharedAssignmentPointer(newAssignment));
}

void DomainServer::populateStaticScriptedAssignmentsFromSettings() {
    const QString PERSISTENT_SCRIPTS_KEY_PATH = "scripts.persistent_scripts";
    const QVariant* persistentScriptsVariant = valueForKeyPath(_settingsManager.getSettingsMap(), PERSISTENT_SCRIPTS_KEY_PATH);
    
    if (persistentScriptsVariant) {
        QVariantList persistentScriptsList = persistentScriptsVariant->toList();
        foreach(const QVariant& persistentScriptVariant, persistentScriptsList) {
            QVariantMap persistentScript = persistentScriptVariant.toMap();
            
            const QString PERSISTENT_SCRIPT_URL_KEY = "url";
            const QString PERSISTENT_SCRIPT_NUM_INSTANCES_KEY = "num_instances";
            const QString PERSISTENT_SCRIPT_POOL_KEY = "pool";
            
            if (persistentScript.contains(PERSISTENT_SCRIPT_URL_KEY)) {
                // check how many instances of this script to add
                
                int numInstances = persistentScript[PERSISTENT_SCRIPT_NUM_INSTANCES_KEY].toInt();
                QString scriptURL = persistentScript[PERSISTENT_SCRIPT_URL_KEY].toString();
                
                QString scriptPool = persistentScript.value(PERSISTENT_SCRIPT_POOL_KEY).toString();
                
                qDebug() << "Adding" << numInstances << "of persistent script at URL" << scriptURL << "- pool" << scriptPool;
                
                for (int i = 0; i < numInstances; ++i) {
                    // add a scripted assignment to the queue for this instance
                    Assignment* scriptAssignment = new Assignment(Assignment::CreateCommand,
                                                                  Assignment::AgentType,
                                                                  scriptPool);
                    scriptAssignment->setPayload(scriptURL.toUtf8());
                    
                    // add it to static hash so we know we have to keep giving it back out
                    addStaticAssignmentToAssignmentHash(scriptAssignment);
                }
            }
        }
    }
}

void DomainServer::createStaticAssignmentsForType(Assignment::Type type, const QVariantList &configList) {
    // we have a string for config for this type
    qDebug() << "Parsing config for assignment type" << type;

    int configCounter = 0;

    foreach(const QVariant& configVariant, configList) {
        if (configVariant.canConvert(QMetaType::QVariantMap)) {
            QVariantMap configMap = configVariant.toMap();

            // check the config string for a pool
            const QString ASSIGNMENT_POOL_KEY = "pool";

            QString assignmentPool = configMap.value(ASSIGNMENT_POOL_KEY).toString();
            if (!assignmentPool.isEmpty()) {
                configMap.remove(ASSIGNMENT_POOL_KEY);
            }

            ++configCounter;
            qDebug() << "Type" << type << "config" << configCounter << "=" << configMap;

            Assignment* configAssignment = new Assignment(Assignment::CreateCommand, type, assignmentPool);

            // setup the payload as a semi-colon separated list of key = value
            QStringList payloadStringList;
            foreach(const QString& payloadKey, configMap.keys()) {
                QString dashes = payloadKey.size() == 1 ? "-" : "--";
                payloadStringList << QString("%1%2 %3").arg(dashes).arg(payloadKey).arg(configMap[payloadKey].toString());
            }
            
            configAssignment->setPayload(payloadStringList.join(' ').toUtf8());

            addStaticAssignmentToAssignmentHash(configAssignment);
        }
    }
}

void DomainServer::populateDefaultStaticAssignmentsExcludingTypes(const QSet<Assignment::Type>& excludedTypes) {
    // enumerate over all assignment types and see if we've already excluded it
    for (Assignment::Type defaultedType = Assignment::AudioMixerType;
         defaultedType != Assignment::AllTypes;
         defaultedType =  static_cast<Assignment::Type>(static_cast<int>(defaultedType) + 1)) {
        if (!excludedTypes.contains(defaultedType) 
            && defaultedType != Assignment::UNUSED_0
            && defaultedType != Assignment::UNUSED_1
            && defaultedType != Assignment::AgentType) {
            // type has not been set from a command line or config file config, use the default
            // by clearing whatever exists and writing a single default assignment with no payload
            Assignment* newAssignment = new Assignment(Assignment::CreateCommand, (Assignment::Type) defaultedType);
            addStaticAssignmentToAssignmentHash(newAssignment);
        }
    }
}

const NodeSet STATICALLY_ASSIGNED_NODES = NodeSet() << NodeType::AudioMixer
    << NodeType::AvatarMixer << NodeType::EntityServer
    << NodeType::MetavoxelServer;

void DomainServer::handleConnectRequest(const QByteArray& packet, const HifiSockAddr& senderSockAddr) {

    NodeType_t nodeType;
    HifiSockAddr publicSockAddr, localSockAddr;
    
    QDataStream packetStream(packet);
    packetStream.skipRawData(numBytesForPacketHeader(packet));

    parseNodeDataFromByteArray(packetStream, nodeType, publicSockAddr, localSockAddr, senderSockAddr);

    QUuid packetUUID = uuidFromPacketHeader(packet);

    // check if this connect request matches an assignment in the queue
    bool isAssignment = _pendingAssignedNodes.contains(packetUUID);
    SharedAssignmentPointer matchingQueuedAssignment = SharedAssignmentPointer();
    PendingAssignedNodeData* pendingAssigneeData = NULL;

    if (isAssignment) {
        pendingAssigneeData = _pendingAssignedNodes.value(packetUUID);

        if (pendingAssigneeData) {
            matchingQueuedAssignment = matchingQueuedAssignmentForCheckIn(pendingAssigneeData->getAssignmentUUID(), nodeType);

            if (matchingQueuedAssignment) {
                qDebug() << "Assignment deployed with" << uuidStringWithoutCurlyBraces(packetUUID)
                    << "matches unfulfilled assignment"
                    << uuidStringWithoutCurlyBraces(matchingQueuedAssignment->getUUID());

                // remove this unique assignment deployment from the hash of pending assigned nodes
                // cleanup of the PendingAssignedNodeData happens below after the node has been added to the LimitedNodeList
                _pendingAssignedNodes.remove(packetUUID);
            } else {
                // this is a node connecting to fulfill an assignment that doesn't exist
                // don't reply back to them so they cycle back and re-request an assignment
                qDebug() << "No match for assignment deployed with" << uuidStringWithoutCurlyBraces(packetUUID);
                return;
            }
        }

    }
    
    QList<NodeType_t> nodeInterestList;
    QString username;
    QByteArray usernameSignature;
    
    packetStream >> nodeInterestList >> username >> usernameSignature;
    
    if (!isAssignment && !shouldAllowConnectionFromNode(username, usernameSignature, senderSockAddr)) {
        // this is an agent and we've decided we won't let them connect - send them a packet to deny connection
        QByteArray usernameRequestByteArray = byteArrayWithPopulatedHeader(PacketTypeDomainConnectionDenied);
        
        // send this oauth request datagram back to the client
        DependencyManager::get<LimitedNodeList>()->writeUnverifiedDatagram(usernameRequestByteArray, senderSockAddr);
        
        return;
    }

    if ((!isAssignment && !STATICALLY_ASSIGNED_NODES.contains(nodeType))
        || (isAssignment && matchingQueuedAssignment)) {
        // this was either not a static assignment or it was and we had a matching one in the queue
        
        QUuid nodeUUID;

        if (_connectingICEPeers.contains(packetUUID) || _connectedICEPeers.contains(packetUUID)) {
            //  this user negotiated a connection with us via ICE, so re-use their ICE client ID
            nodeUUID = packetUUID;
        } else {
            // we got a packetUUID we didn't recognize, just add the node
            nodeUUID = QUuid::createUuid();
        }
        

        SharedNodePointer newNode = DependencyManager::get<LimitedNodeList>()->addOrUpdateNode(nodeUUID, nodeType,
                                                                                    publicSockAddr, localSockAddr);
        // when the newNode is created the linked data is also created
        // if this was a static assignment set the UUID, set the sendingSockAddr
        DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(newNode->getLinkedData());

        if (isAssignment) {
            nodeData->setAssignmentUUID(matchingQueuedAssignment->getUUID());
            nodeData->setWalletUUID(pendingAssigneeData->getWalletUUID());

            // now that we've pulled the wallet UUID and added the node to our list, delete the pending assignee data
            delete pendingAssigneeData;
        }
        
        // if we have a username from an OAuth connect request, set it on the DomainServerNodeData
        nodeData->setUsername(username);
        nodeData->setSendingSockAddr(senderSockAddr);

        // reply back to the user with a PacketTypeDomainList
        sendDomainListToNode(newNode, senderSockAddr, nodeInterestList.toSet());
    }
}

const QString ALLOWED_USERS_SETTINGS_KEYPATH = "security.allowed_users";

bool DomainServer::shouldAllowConnectionFromNode(const QString& username,
                                                 const QByteArray& usernameSignature,
                                                 const HifiSockAddr& senderSockAddr) {
    const QVariant* allowedUsersVariant = valueForKeyPath(_settingsManager.getSettingsMap(),
                                                                 ALLOWED_USERS_SETTINGS_KEYPATH);
    QStringList allowedUsers = allowedUsersVariant ? allowedUsersVariant->toStringList() : QStringList();
    
    // we always let in a user who is sending a packet from our local socket or from the localhost address
    if (senderSockAddr.getAddress() == DependencyManager::get<LimitedNodeList>()->getLocalSockAddr().getAddress()
        || senderSockAddr.getAddress() == QHostAddress::LocalHost) {
        return true;
    }
    
    if (allowedUsers.count() > 0) {
        if (allowedUsers.contains(username, Qt::CaseInsensitive)) {
            // it's possible this user can be allowed to connect, but we need to check their username signature
            
            QByteArray publicKeyArray = _userPublicKeys.value(username);
            if (!publicKeyArray.isEmpty()) {
                // if we do have a public key for the user, check for a signature match
                
                const unsigned char* publicKeyData = reinterpret_cast<const unsigned char*>(publicKeyArray.constData());
                
                // first load up the public key into an RSA struct
                RSA* rsaPublicKey = d2i_RSA_PUBKEY(NULL, &publicKeyData, publicKeyArray.size());
                
                if (rsaPublicKey) {
                    QByteArray decryptedArray(RSA_size(rsaPublicKey), 0);
                    int decryptResult = RSA_public_decrypt(usernameSignature.size(),
                                                           reinterpret_cast<const unsigned char*>(usernameSignature.constData()),
                                                           reinterpret_cast<unsigned char*>(decryptedArray.data()),
                                                           rsaPublicKey, RSA_PKCS1_PADDING);
                    
                    if (decryptResult != -1) {
                        if (username.toLower() == decryptedArray) {
                            qDebug() << "Username signature matches for" << username << "- allowing connection.";
                            
                            // free up the public key before we return
                            RSA_free(rsaPublicKey);
                            
                            return true;
                        } else {
                            qDebug() << "Username signature did not match for" << username << "- denying connection.";
                        }
                    } else {
                        qDebug() << "Couldn't decrypt user signature for" << username << "- denying connection.";
                    }
                    
                    // free up the public key, we don't need it anymore
                    RSA_free(rsaPublicKey);
                } else {
                    // we can't let this user in since we couldn't convert their public key to an RSA key we could use
                    qDebug() << "Couldn't convert data to RSA key for" << username << "- denying connection.";
                }
            }
        
            requestUserPublicKey(username);
        } else {
            qDebug() << "Connect request denied for user" << username << "not in allowed users list.";
        }
    } else {
        // since we have no allowed user list, let them all in
        return true;
    }
    
    return false;
}

void DomainServer::preloadAllowedUserPublicKeys() {
    const QVariant* allowedUsersVariant = valueForKeyPath(_settingsManager.getSettingsMap(), ALLOWED_USERS_SETTINGS_KEYPATH);
    QStringList allowedUsers = allowedUsersVariant ? allowedUsersVariant->toStringList() : QStringList();
    
    if (allowedUsers.size() > 0) {
        // in the future we may need to limit how many requests here - for now assume that lists of allowed users are not
        // going to create > 100 requests
        foreach(const QString& username, allowedUsers) {
            requestUserPublicKey(username);
        }
    }
}

void DomainServer::requestUserPublicKey(const QString& username) {
    // even if we have a public key for them right now, request a new one in case it has just changed
    JSONCallbackParameters callbackParams;
    callbackParams.jsonCallbackReceiver = this;
    callbackParams.jsonCallbackMethod = "publicKeyJSONCallback";
    
    const QString USER_PUBLIC_KEY_PATH = "api/v1/users/%1/public_key";
    
    qDebug() << "Requesting public key for user" << username;
    
    AccountManager::getInstance().unauthenticatedRequest(USER_PUBLIC_KEY_PATH.arg(username),
                                                         QNetworkAccessManager::GetOperation, callbackParams);
}

QUrl DomainServer::oauthRedirectURL() {
    return QString("https://%1:%2/oauth").arg(_hostname).arg(_httpsManager->serverPort());
}

const QString OAUTH_CLIENT_ID_QUERY_KEY = "client_id";
const QString OAUTH_REDIRECT_URI_QUERY_KEY = "redirect_uri";

QUrl DomainServer::oauthAuthorizationURL(const QUuid& stateUUID) {
    // for now these are all interface clients that have a GUI
    // so just send them back the full authorization URL
    QUrl authorizationURL = _oauthProviderURL;

    const QString OAUTH_AUTHORIZATION_PATH = "/oauth/authorize";
    authorizationURL.setPath(OAUTH_AUTHORIZATION_PATH);

    QUrlQuery authorizationQuery;

    authorizationQuery.addQueryItem(OAUTH_CLIENT_ID_QUERY_KEY, _oauthClientID);

    const QString OAUTH_RESPONSE_TYPE_QUERY_KEY = "response_type";
    const QString OAUTH_REPSONSE_TYPE_QUERY_VALUE = "code";
    authorizationQuery.addQueryItem(OAUTH_RESPONSE_TYPE_QUERY_KEY, OAUTH_REPSONSE_TYPE_QUERY_VALUE);

    const QString OAUTH_STATE_QUERY_KEY = "state";
    // create a new UUID that will be the state parameter for oauth authorization AND the new session UUID for that node
    authorizationQuery.addQueryItem(OAUTH_STATE_QUERY_KEY, uuidStringWithoutCurlyBraces(stateUUID));

    authorizationQuery.addQueryItem(OAUTH_REDIRECT_URI_QUERY_KEY, oauthRedirectURL().toString());

    authorizationURL.setQuery(authorizationQuery);

    return authorizationURL;
}

int DomainServer::parseNodeDataFromByteArray(QDataStream& packetStream, NodeType_t& nodeType,
                                             HifiSockAddr& publicSockAddr, HifiSockAddr& localSockAddr,
                                             const HifiSockAddr& senderSockAddr) {
    packetStream >> nodeType;
    packetStream >> publicSockAddr >> localSockAddr;

    if (publicSockAddr.getAddress().isNull()) {
        // this node wants to use us its STUN server
        // so set the node public address to whatever we perceive the public address to be

        // if the sender is on our box then leave its public address to 0 so that
        // other users attempt to reach it on the same address they have for the domain-server
        if (senderSockAddr.getAddress().isLoopback()) {
            publicSockAddr.setAddress(QHostAddress());
        } else {
            publicSockAddr.setAddress(senderSockAddr.getAddress());
        }
    }

    return packetStream.device()->pos();
}

NodeSet DomainServer::nodeInterestListFromPacket(const QByteArray& packet, int numPreceedingBytes) {
    QDataStream packetStream(packet);
    packetStream.skipRawData(numPreceedingBytes);

    quint8 numInterestTypes = 0;
    packetStream >> numInterestTypes;

    quint8 nodeType;
    NodeSet nodeInterestSet;

    for (int i = 0; i < numInterestTypes; i++) {
        packetStream >> nodeType;
        nodeInterestSet.insert((NodeType_t) nodeType);
    }

    return nodeInterestSet;
}

void DomainServer::sendDomainListToNode(const SharedNodePointer& node, const HifiSockAddr &senderSockAddr,
                                        const NodeSet& nodeInterestList) {

    QByteArray broadcastPacket = byteArrayWithPopulatedHeader(PacketTypeDomainList);

    // always send the node their own UUID back
    QDataStream broadcastDataStream(&broadcastPacket, QIODevice::Append);
    broadcastDataStream << node->getUUID();

    int numBroadcastPacketLeadBytes = broadcastDataStream.device()->pos();

    DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
    
    auto nodeList = DependencyManager::get<LimitedNodeList>();
    
    // if we've established a connection via ICE with this peer, use that socket
    // otherwise just try to reply back to them on their sending socket (although that may not work)
    HifiSockAddr destinationSockAddr = _connectedICEPeers.value(node->getUUID());
    if (destinationSockAddr.isNull()) {
        destinationSockAddr = senderSockAddr;
    }

    if (nodeInterestList.size() > 0) {

//        DTLSServerSession* dtlsSession = _isUsingDTLS ? _dtlsSessions[senderSockAddr] : NULL;
        int dataMTU = MAX_PACKET_SIZE;

        if (nodeData->isAuthenticated()) {
            // if this authenticated node has any interest types, send back those nodes as well
            nodeList->eachNode([&](const SharedNodePointer& otherNode){
                // reset our nodeByteArray and nodeDataStream
                QByteArray nodeByteArray;
                QDataStream nodeDataStream(&nodeByteArray, QIODevice::Append);
                
                if (otherNode->getUUID() != node->getUUID() && nodeInterestList.contains(otherNode->getType())) {
                    
                    // don't send avatar nodes to other avatars, that will come from avatar mixer
                    nodeDataStream << *otherNode.data();
                    
                    // pack the secret that these two nodes will use to communicate with each other
                    QUuid secretUUID = nodeData->getSessionSecretHash().value(otherNode->getUUID());
                    if (secretUUID.isNull()) {
                        // generate a new secret UUID these two nodes can use
                        secretUUID = QUuid::createUuid();
                        
                        // set that on the current Node's sessionSecretHash
                        nodeData->getSessionSecretHash().insert(otherNode->getUUID(), secretUUID);
                        
                        // set it on the other Node's sessionSecretHash
                        reinterpret_cast<DomainServerNodeData*>(otherNode->getLinkedData())
                        ->getSessionSecretHash().insert(node->getUUID(), secretUUID);
                        
                    }
                    
                    nodeDataStream << secretUUID;
                    
                    if (broadcastPacket.size() +  nodeByteArray.size() > dataMTU) {
                        // we need to break here and start a new packet
                        // so send the current one
                        
                        nodeList->writeDatagram(broadcastPacket, node, senderSockAddr);
                        
                        // reset the broadcastPacket structure
                        broadcastPacket.resize(numBroadcastPacketLeadBytes);
                        broadcastDataStream.device()->seek(numBroadcastPacketLeadBytes);
                    }
                    
                    // append the nodeByteArray to the current state of broadcastDataStream
                    broadcastPacket.append(nodeByteArray);
                }
            });
        }

        // always write the last broadcastPacket
        nodeList->writeDatagram(broadcastPacket, node, senderSockAddr);
    }
}

void DomainServer::readAvailableDatagrams() {
    auto nodeList = DependencyManager::get<LimitedNodeList>();

    HifiSockAddr senderSockAddr;
    QByteArray receivedPacket;

    static QByteArray assignmentPacket = byteArrayWithPopulatedHeader(PacketTypeCreateAssignment);
    static int numAssignmentPacketHeaderBytes = assignmentPacket.size();

    while (nodeList->getNodeSocket().hasPendingDatagrams()) {
        receivedPacket.resize(nodeList->getNodeSocket().pendingDatagramSize());
        nodeList->getNodeSocket().readDatagram(receivedPacket.data(), receivedPacket.size(),
                                               senderSockAddr.getAddressPointer(), senderSockAddr.getPortPointer());
        if (packetTypeForPacket(receivedPacket) == PacketTypeRequestAssignment
            && nodeList->packetVersionAndHashMatch(receivedPacket)) {

            // construct the requested assignment from the packet data
            Assignment requestAssignment(receivedPacket);

            // Suppress these for Assignment::AgentType to once per 5 seconds
            static QElapsedTimer noisyMessageTimer;
            static bool wasNoisyTimerStarted = false;

            if (!wasNoisyTimerStarted) {
                noisyMessageTimer.start();
                wasNoisyTimerStarted = true;
            }

            const qint64 NOISY_MESSAGE_INTERVAL_MSECS = 5 * 1000;

            if (requestAssignment.getType() != Assignment::AgentType
                || noisyMessageTimer.elapsed() > NOISY_MESSAGE_INTERVAL_MSECS) {
                qDebug() << "Received a request for assignment type" << requestAssignment.getType()
                    << "from" << senderSockAddr;
                noisyMessageTimer.restart();
            }

            SharedAssignmentPointer assignmentToDeploy = deployableAssignmentForRequest(requestAssignment);

            if (assignmentToDeploy) {
                qDebug() << "Deploying assignment -" << *assignmentToDeploy.data() << "- to" << senderSockAddr;

                // give this assignment out, either the type matches or the requestor said they will take any
                assignmentPacket.resize(numAssignmentPacketHeaderBytes);

                // setup a copy of this assignment that will have a unique UUID, for packaging purposes
                Assignment uniqueAssignment(*assignmentToDeploy.data());
                uniqueAssignment.setUUID(QUuid::createUuid());

                QDataStream assignmentStream(&assignmentPacket, QIODevice::Append);

                assignmentStream << uniqueAssignment;

                nodeList->getNodeSocket().writeDatagram(assignmentPacket,
                                                        senderSockAddr.getAddress(), senderSockAddr.getPort());

                // add the information for that deployed assignment to the hash of pending assigned nodes
                PendingAssignedNodeData* pendingNodeData = new PendingAssignedNodeData(assignmentToDeploy->getUUID(),
                                                                                       requestAssignment.getWalletUUID());
                _pendingAssignedNodes.insert(uniqueAssignment.getUUID(), pendingNodeData);
            } else {
                if (requestAssignment.getType() != Assignment::AgentType
                    || noisyMessageTimer.elapsed() > NOISY_MESSAGE_INTERVAL_MSECS) {
                    qDebug() << "Unable to fulfill assignment request of type" << requestAssignment.getType()
                        << "from" << senderSockAddr;
                    noisyMessageTimer.restart();
                }
            }
        } else if (!_isUsingDTLS) {
            // not using DTLS, process datagram normally
            processDatagram(receivedPacket, senderSockAddr);
        } else {
            // we're using DTLS, so tell the sender to get back to us using DTLS
            static QByteArray dtlsRequiredPacket = byteArrayWithPopulatedHeader(PacketTypeDomainServerRequireDTLS);
            static int numBytesDTLSHeader = numBytesForPacketHeaderGivenPacketType(PacketTypeDomainServerRequireDTLS);

            if (dtlsRequiredPacket.size() == numBytesDTLSHeader) {
                // pack the port that we accept DTLS traffic on
                unsigned short dtlsPort = nodeList->getDTLSSocket().localPort();
                dtlsRequiredPacket.replace(numBytesDTLSHeader, sizeof(dtlsPort), reinterpret_cast<const char*>(&dtlsPort));
            }

            nodeList->writeUnverifiedDatagram(dtlsRequiredPacket, senderSockAddr);
        }
    }
}

void DomainServer::setupPendingAssignmentCredits() {
    // enumerate the NodeList to find the assigned nodes
    DependencyManager::get<LimitedNodeList>()->eachNode([&](const SharedNodePointer& node){
        DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
        
        if (!nodeData->getAssignmentUUID().isNull() && !nodeData->getWalletUUID().isNull()) {
            // check if we have a non-finalized transaction for this node to add this amount to
            TransactionHash::iterator i = _pendingAssignmentCredits.find(nodeData->getWalletUUID());
            WalletTransaction* existingTransaction = NULL;
            
            while (i != _pendingAssignmentCredits.end() && i.key() == nodeData->getWalletUUID()) {
                if (!i.value()->isFinalized()) {
                    existingTransaction = i.value();
                    break;
                } else {
                    ++i;
                }
            }
            
            qint64 elapsedMsecsSinceLastPayment = nodeData->getPaymentIntervalTimer().elapsed();
            nodeData->getPaymentIntervalTimer().restart();
            
            const float CREDITS_PER_HOUR = 0.10f;
            const float CREDITS_PER_MSEC = CREDITS_PER_HOUR / (60 * 60 * 1000);
            const int SATOSHIS_PER_MSEC = CREDITS_PER_MSEC * SATOSHIS_PER_CREDIT;
            
            float pendingCredits = elapsedMsecsSinceLastPayment * SATOSHIS_PER_MSEC;
            
            if (existingTransaction) {
                existingTransaction->incrementAmount(pendingCredits);
            } else {
                // create a fresh transaction to pay this node, there is no transaction to append to
                WalletTransaction* freshTransaction = new WalletTransaction(nodeData->getWalletUUID(), pendingCredits);
                _pendingAssignmentCredits.insert(nodeData->getWalletUUID(), freshTransaction);
            }
        }
    });
}

void DomainServer::sendPendingTransactionsToServer() {

    AccountManager& accountManager = AccountManager::getInstance();

    if (accountManager.hasValidAccessToken()) {

        // enumerate the pending transactions and send them to the server to complete payment
        TransactionHash::iterator i = _pendingAssignmentCredits.begin();

        JSONCallbackParameters transactionCallbackParams;

        transactionCallbackParams.jsonCallbackReceiver = this;
        transactionCallbackParams.jsonCallbackMethod = "transactionJSONCallback";

        while (i != _pendingAssignmentCredits.end()) {
            accountManager.authenticatedRequest("api/v1/transactions", QNetworkAccessManager::PostOperation,
                                                transactionCallbackParams, i.value()->postJson().toJson());

            // set this transaction to finalized so we don't add additional credits to it
            i.value()->setIsFinalized(true);

            ++i;
        }
    }
}

void DomainServer::publicKeyJSONCallback(QNetworkReply& requestReply) {
    QJsonObject jsonObject = QJsonDocument::fromJson(requestReply.readAll()).object();
    
    if (jsonObject["status"].toString() == "success") {
        // figure out which user this is for
        
        const QString PUBLIC_KEY_URL_REGEX_STRING = "api\\/v1\\/users\\/([A-Za-z0-9_\\.]+)\\/public_key";
        QRegExp usernameRegex(PUBLIC_KEY_URL_REGEX_STRING);
        
        if (usernameRegex.indexIn(requestReply.url().toString()) != -1) {
            QString username = usernameRegex.cap(1);
            
            qDebug() << "Storing a public key for user" << username;
            
            // pull the public key as a QByteArray from this response
            const QString JSON_DATA_KEY = "data";
            const QString JSON_PUBLIC_KEY_KEY = "public_key";
            
            _userPublicKeys[username] =
                QByteArray::fromBase64(jsonObject[JSON_DATA_KEY].toObject()[JSON_PUBLIC_KEY_KEY].toString().toUtf8());
        }
    }
}

void DomainServer::transactionJSONCallback(const QJsonObject& data) {
    // check if this was successful - if so we can remove it from our list of pending
    if (data.value("status").toString() == "success") {
        // create a dummy wallet transaction to unpack the JSON to
        WalletTransaction dummyTransaction;
        dummyTransaction.loadFromJson(data);

        TransactionHash::iterator i = _pendingAssignmentCredits.find(dummyTransaction.getDestinationUUID());

        while (i != _pendingAssignmentCredits.end() && i.key() == dummyTransaction.getDestinationUUID()) {
            if (i.value()->getUUID() == dummyTransaction.getUUID()) {
                // we have a match - we can remove this from the hash of pending credits
                // and delete it for clean up

                WalletTransaction* matchingTransaction = i.value();
                _pendingAssignmentCredits.erase(i);
                delete matchingTransaction;

                break;
            } else {
                ++i;
            }
        }
    }
}

void DomainServer::requestCurrentPublicSocketViaSTUN() {
    DependencyManager::get<LimitedNodeList>()->sendSTUNRequest();
}

QJsonObject jsonForDomainSocketUpdate(const HifiSockAddr& socket) {
    const QString SOCKET_NETWORK_ADDRESS_KEY = "network_address";
    const QString SOCKET_PORT_KEY = "port";
    
    QJsonObject socketObject;
    socketObject[SOCKET_NETWORK_ADDRESS_KEY] = socket.getAddress().toString();
    socketObject[SOCKET_PORT_KEY] = socket.getPort();
    
    return socketObject;
}

const QString DOMAIN_UPDATE_AUTOMATIC_NETWORKING_KEY = "automatic_networking";

void DomainServer::performIPAddressUpdate(const HifiSockAddr& newPublicSockAddr) {
    sendHeartbeatToDataServer(newPublicSockAddr.getAddress().toString());
}

void DomainServer::sendHeartbeatToDataServer(const QString& networkAddress) {
    const QString DOMAIN_UPDATE = "/api/v1/domains/%1";
    auto nodeList = DependencyManager::get<LimitedNodeList>();
    const QUuid& domainID = nodeList->getSessionUUID();
    
    // setup the domain object to send to the data server
    const QString PUBLIC_NETWORK_ADDRESS_KEY = "network_address";
    const QString AUTOMATIC_NETWORKING_KEY = "automatic_networking";
    
    QJsonObject domainObject;
    if (!networkAddress.isEmpty()) {
        domainObject[PUBLIC_NETWORK_ADDRESS_KEY] = networkAddress;
    }
    
    domainObject[AUTOMATIC_NETWORKING_KEY] = _automaticNetworkingSetting;
    
    // add a flag to indicate if this domain uses restricted access - for now that will exclude it from listings
    const QString RESTRICTED_ACCESS_FLAG = "restricted";
    
    const QVariant* allowedUsersVariant = valueForKeyPath(_settingsManager.getSettingsMap(),
                                                          ALLOWED_USERS_SETTINGS_KEYPATH);
    QStringList allowedUsers = allowedUsersVariant ? allowedUsersVariant->toStringList() : QStringList();
    domainObject[RESTRICTED_ACCESS_FLAG] = (allowedUsers.size() > 0);
    
    // add the number of currently connected agent users
    int numConnectedAuthedUsers = 0;
    
    nodeList->eachNode([&numConnectedAuthedUsers](const SharedNodePointer& node){
        if (node->getLinkedData() && !static_cast<DomainServerNodeData*>(node->getLinkedData())->getUsername().isEmpty()) {
            ++numConnectedAuthedUsers;
        }
    });
    
    const QString DOMAIN_HEARTBEAT_KEY = "heartbeat";
    const QString HEARTBEAT_NUM_USERS_KEY = "num_users";
    
    QJsonObject heartbeatObject;
    heartbeatObject[HEARTBEAT_NUM_USERS_KEY] = numConnectedAuthedUsers;
    domainObject[DOMAIN_HEARTBEAT_KEY] = heartbeatObject;
    
    QString domainUpdateJSON = QString("{\"domain\": %1 }").arg(QString(QJsonDocument(domainObject).toJson()));
    
    AccountManager::getInstance().authenticatedRequest(DOMAIN_UPDATE.arg(uuidStringWithoutCurlyBraces(domainID)),
                                                       QNetworkAccessManager::PutOperation,
                                                       JSONCallbackParameters(),
                                                       domainUpdateJSON.toUtf8());
}

// todo: have data-web respond with ice-server hostname to use

void DomainServer::performICEUpdates() {
    sendHeartbeatToIceServer();
    sendICEPingPackets();
}

void DomainServer::sendHeartbeatToIceServer() {
    DependencyManager::get<LimitedNodeList>()->sendHeartbeatToIceServer(_iceServerSocket);
}

void DomainServer::sendICEPingPackets() {
    auto nodeList = DependencyManager::get<LimitedNodeList>();
    
    QHash<QUuid, NetworkPeer>::iterator peer = _connectingICEPeers.begin();
    
    while (peer != _connectingICEPeers.end()) {
        
        if (peer->getConnectionAttempts() >= MAX_ICE_CONNECTION_ATTEMPTS) {
            // we've already tried to connect to this peer enough times
            // remove it from our list - if it wants to re-connect it'll come back through ice-server
            peer = _connectingICEPeers.erase(peer);
        } else {
            // send ping packets to this peer's interfaces
            qDebug() << "Sending ping packets to establish connectivity with ICE peer with ID"
                << peer->getUUID();
            
            // send the ping packet to the local and public sockets for this node
            QByteArray localPingPacket = nodeList->constructPingPacket(PingType::Local, false);
            nodeList->writeUnverifiedDatagram(localPingPacket, peer->getLocalSocket());
            
            QByteArray publicPingPacket = nodeList->constructPingPacket(PingType::Public, false);
            nodeList->writeUnverifiedDatagram(publicPingPacket, peer->getPublicSocket());
            
            peer->incrementConnectionAttempts();
            
            // go to next peer in hash
            ++peer;
        }
    }
}

void DomainServer::processICEHeartbeatResponse(const QByteArray& packet) {
    // loop through the packet and pull out network peers
    // any peer we don't have we add to the hash, otherwise we update
    QDataStream iceResponseStream(packet);
    iceResponseStream.skipRawData(numBytesForPacketHeader(packet));
    
    NetworkPeer receivedPeer;
    
    while (!iceResponseStream.atEnd()) {
        iceResponseStream >> receivedPeer;
        
        if (!_connectedICEPeers.contains(receivedPeer.getUUID())) {
            if (!_connectingICEPeers.contains(receivedPeer.getUUID())) {
                qDebug() << "New peer requesting connection being added to hash -" << receivedPeer;
            }
            
            _connectingICEPeers[receivedPeer.getUUID()] = receivedPeer;
        }
    }
}

void DomainServer::processICEPingReply(const QByteArray& packet, const HifiSockAddr& senderSockAddr) {
    QUuid nodeUUID = uuidFromPacketHeader(packet);
    NetworkPeer sendingPeer = _connectingICEPeers.take(nodeUUID);
    
    if (!sendingPeer.isNull()) {
        // we had this NetworkPeer in our connecting list - add the right sock addr to our connected list
        if (senderSockAddr == sendingPeer.getLocalSocket()) {
            qDebug() << "Activating local socket for communication with network peer -" << sendingPeer;
            _connectedICEPeers.insert(nodeUUID, sendingPeer.getLocalSocket());
        } else if (senderSockAddr == sendingPeer.getPublicSocket()) {
            qDebug() << "Activating public socket for communication with network peer -" << sendingPeer;
            _connectedICEPeers.insert(nodeUUID, sendingPeer.getPublicSocket());
        }
    }
}

void DomainServer::processDatagram(const QByteArray& receivedPacket, const HifiSockAddr& senderSockAddr) {
    auto nodeList = DependencyManager::get<LimitedNodeList>();

    if (nodeList->packetVersionAndHashMatch(receivedPacket)) {
        PacketType requestType = packetTypeForPacket(receivedPacket);
        switch (requestType) {
            case PacketTypeDomainConnectRequest:
                handleConnectRequest(receivedPacket, senderSockAddr);
                break;
            case PacketTypeDomainListRequest: {
                QUuid nodeUUID = uuidFromPacketHeader(receivedPacket);
                
                if (!nodeUUID.isNull() && nodeList->nodeWithUUID(nodeUUID)) {
                    NodeType_t throwawayNodeType;
                    HifiSockAddr nodePublicAddress, nodeLocalAddress;
                
                    QDataStream packetStream(receivedPacket);
                    packetStream.skipRawData(numBytesForPacketHeader(receivedPacket));
                    
                    parseNodeDataFromByteArray(packetStream, throwawayNodeType, nodePublicAddress, nodeLocalAddress,
                                               senderSockAddr);
                    
                    SharedNodePointer checkInNode = nodeList->nodeWithUUID(nodeUUID);
                    checkInNode->setPublicSocket(nodePublicAddress);
                    checkInNode->setLocalSocket(nodeLocalAddress);
                    
                    // update last receive to now
                    quint64 timeNow = usecTimestampNow();
                    checkInNode->setLastHeardMicrostamp(timeNow);
                    
                    QList<NodeType_t> nodeInterestList;
                    packetStream >> nodeInterestList;
                    
                    sendDomainListToNode(checkInNode, senderSockAddr, nodeInterestList.toSet());
                }
                
                break;
            }
            case PacketTypeNodeJsonStats: {
                SharedNodePointer matchingNode = nodeList->sendingNodeForPacket(receivedPacket);
                if (matchingNode) {
                    reinterpret_cast<DomainServerNodeData*>(matchingNode->getLinkedData())->parseJSONStatsPacket(receivedPacket);
                }
                
                break;
            }
            case PacketTypeStunResponse:
                nodeList->processSTUNResponse(receivedPacket);
                break;
            case PacketTypeUnverifiedPing: {
                QByteArray pingReplyPacket = nodeList->constructPingReplyPacket(receivedPacket);
                nodeList->writeUnverifiedDatagram(pingReplyPacket, senderSockAddr);
                
                break;
            }
            case PacketTypeUnverifiedPingReply: {
                processICEPingReply(receivedPacket, senderSockAddr);
                break;
            }
            case PacketTypeIceServerHeartbeatResponse:
                processICEHeartbeatResponse(receivedPacket);
                break;
            default:
                break;
        }
    }
}

QJsonObject DomainServer::jsonForSocket(const HifiSockAddr& socket) {
    QJsonObject socketJSON;

    socketJSON["ip"] = socket.getAddress().toString();
    socketJSON["port"] = socket.getPort();

    return socketJSON;
}

const char JSON_KEY_UUID[] = "uuid";
const char JSON_KEY_TYPE[] = "type";
const char JSON_KEY_PUBLIC_SOCKET[] = "public";
const char JSON_KEY_LOCAL_SOCKET[] = "local";
const char JSON_KEY_POOL[] = "pool";
const char JSON_KEY_PENDING_CREDITS[] = "pending_credits";
const char JSON_KEY_WAKE_TIMESTAMP[] = "wake_timestamp";
const char JSON_KEY_USERNAME[] = "username";

QJsonObject DomainServer::jsonObjectForNode(const SharedNodePointer& node) {
    QJsonObject nodeJson;

    // re-format the type name so it matches the target name
    QString nodeTypeName = NodeType::getNodeTypeName(node->getType());
    nodeTypeName = nodeTypeName.toLower();
    nodeTypeName.replace(' ', '-');

    // add the node UUID
    nodeJson[JSON_KEY_UUID] = uuidStringWithoutCurlyBraces(node->getUUID());

    // add the node type
    nodeJson[JSON_KEY_TYPE] = nodeTypeName;

    // add the node socket information
    nodeJson[JSON_KEY_PUBLIC_SOCKET] = jsonForSocket(node->getPublicSocket());
    nodeJson[JSON_KEY_LOCAL_SOCKET] = jsonForSocket(node->getLocalSocket());

    // add the node uptime in our list
    nodeJson[JSON_KEY_WAKE_TIMESTAMP] = QString::number(node->getWakeTimestamp());

    // if the node has pool information, add it
    DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
    
    // add the node username, if it exists
    nodeJson[JSON_KEY_USERNAME] = nodeData->getUsername();
    
    SharedAssignmentPointer matchingAssignment = _allAssignments.value(nodeData->getAssignmentUUID());
    if (matchingAssignment) {
        nodeJson[JSON_KEY_POOL] = matchingAssignment->getPool();

        if (!nodeData->getWalletUUID().isNull()) {
            TransactionHash::iterator i = _pendingAssignmentCredits.find(nodeData->getWalletUUID());
            float pendingCreditAmount = 0;

            while (i != _pendingAssignmentCredits.end() && i.key() == nodeData->getWalletUUID()) {
                pendingCreditAmount += i.value()->getAmount() / SATOSHIS_PER_CREDIT;
                ++i;
            }

            nodeJson[JSON_KEY_PENDING_CREDITS] = pendingCreditAmount;
        }
    }

    return nodeJson;
}

const char ASSIGNMENT_SCRIPT_HOST_LOCATION[] = "resources/web/assignment";

QString pathForAssignmentScript(const QUuid& assignmentUUID) {
    QString newPath(ASSIGNMENT_SCRIPT_HOST_LOCATION);
    newPath += "/scripts/";
    // append the UUID for this script as the new filename, remove the curly braces
    newPath += uuidStringWithoutCurlyBraces(assignmentUUID);
    return newPath;
}

bool DomainServer::handleHTTPRequest(HTTPConnection* connection, const QUrl& url, bool skipSubHandler) {
    const QString JSON_MIME_TYPE = "application/json";

    const QString URI_ASSIGNMENT = "/assignment";
    const QString URI_ASSIGNMENT_SCRIPTS = URI_ASSIGNMENT + "/scripts";
    const QString URI_NODES = "/nodes";

    const QString UUID_REGEX_STRING = "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}";
    
    auto nodeList = DependencyManager::get<LimitedNodeList>();
    
    // allow sub-handlers to handle requests that do not require authentication
    if (_settingsManager.handlePublicHTTPRequest(connection, url)) {
        return true;
    }
    
    // check if this is a request for a scripted assignment (with a temp unique UUID)
    const QString ASSIGNMENT_REGEX_STRING = QString("\\%1\\/(%2)\\/?$").arg(URI_ASSIGNMENT).arg(UUID_REGEX_STRING);
    QRegExp assignmentRegex(ASSIGNMENT_REGEX_STRING);
    
    if (connection->requestOperation() == QNetworkAccessManager::GetOperation
        && assignmentRegex.indexIn(url.path()) != -1) {
        QUuid matchingUUID = QUuid(assignmentRegex.cap(1));
        
        SharedAssignmentPointer matchingAssignment = _allAssignments.value(matchingUUID);
        if (!matchingAssignment) {
            // check if we have a pending assignment that matches this temp UUID, and it is a scripted assignment
            PendingAssignedNodeData* pendingData = _pendingAssignedNodes.value(matchingUUID);
            if (pendingData) {
                matchingAssignment = _allAssignments.value(pendingData->getAssignmentUUID());
                
                if (matchingAssignment && matchingAssignment->getType() == Assignment::AgentType) {
                    // we have a matching assignment and it is for the right type, have the HTTP manager handle it
                    // via correct URL for the script so the client can download
                    
                    QUrl scriptURL = url;
                    scriptURL.setPath(URI_ASSIGNMENT + "/scripts/"
                                      + uuidStringWithoutCurlyBraces(pendingData->getAssignmentUUID()));
                    
                    // have the HTTPManager serve the appropriate script file
                    return _httpManager.handleHTTPRequest(connection, scriptURL, true);
                }
            }
        }
        
        // request not handled
        return false;
    }
    
    // check if this is a request for our domain ID
    const QString URI_ID = "/id";
    if (connection->requestOperation() == QNetworkAccessManager::GetOperation
        && url.path() == URI_ID) {
        QUuid domainID = nodeList->getSessionUUID();
        
        connection->respond(HTTPConnection::StatusCode200, uuidStringWithoutCurlyBraces(domainID).toLocal8Bit());
        return true;
    }
    
    // all requests below require a cookie to prove authentication so check that first
    if (!isAuthenticatedRequest(connection, url)) {
        // this is not an authenticated request
        // return true from the handler since it was handled with a 401 or re-direct to auth
        return true;
    }
    
    if (connection->requestOperation() == QNetworkAccessManager::GetOperation) {
        if (url.path() == "/assignments.json") {
            // user is asking for json list of assignments

            // setup the JSON
            QJsonObject assignmentJSON;
            QJsonObject assignedNodesJSON;

            // enumerate the NodeList to find the assigned nodes
            nodeList->eachNode([this, &assignedNodesJSON](const SharedNodePointer& node){
                DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
                
                if (!nodeData->getAssignmentUUID().isNull()) {
                    // add the node using the UUID as the key
                    QString uuidString = uuidStringWithoutCurlyBraces(nodeData->getAssignmentUUID());
                    assignedNodesJSON[uuidString] = jsonObjectForNode(node);
                }
            });

            assignmentJSON["fulfilled"] = assignedNodesJSON;

            QJsonObject queuedAssignmentsJSON;

            // add the queued but unfilled assignments to the json
            foreach(const SharedAssignmentPointer& assignment, _unfulfilledAssignments) {
                QJsonObject queuedAssignmentJSON;

                QString uuidString = uuidStringWithoutCurlyBraces(assignment->getUUID());
                queuedAssignmentJSON[JSON_KEY_TYPE] = QString(assignment->getTypeName());

                // if the assignment has a pool, add it
                if (!assignment->getPool().isEmpty()) {
                    queuedAssignmentJSON[JSON_KEY_POOL] = assignment->getPool();
                }

                // add this queued assignment to the JSON
                queuedAssignmentsJSON[uuidString] = queuedAssignmentJSON;
            }

            assignmentJSON["queued"] = queuedAssignmentsJSON;

            // print out the created JSON
            QJsonDocument assignmentDocument(assignmentJSON);
            connection->respond(HTTPConnection::StatusCode200, assignmentDocument.toJson(), qPrintable(JSON_MIME_TYPE));

            // we've processed this request
            return true;
        } else if (url.path() == "/transactions.json") {
            // enumerate our pending transactions and display them in an array
            QJsonObject rootObject;
            QJsonArray transactionArray;

            TransactionHash::iterator i = _pendingAssignmentCredits.begin();
            while (i != _pendingAssignmentCredits.end()) {
                transactionArray.push_back(i.value()->toJson());
                ++i;
            }

            rootObject["pending_transactions"] = transactionArray;

            // print out the created JSON
            QJsonDocument transactionsDocument(rootObject);
            connection->respond(HTTPConnection::StatusCode200, transactionsDocument.toJson(), qPrintable(JSON_MIME_TYPE));

            return true;
        } else if (url.path() == QString("%1.json").arg(URI_NODES)) {
            // setup the JSON
            QJsonObject rootJSON;
            QJsonArray nodesJSONArray;

            // enumerate the NodeList to find the assigned nodes
            nodeList->eachNode([this, &nodesJSONArray](const SharedNodePointer& node){
                // add the node using the UUID as the key
                nodesJSONArray.append(jsonObjectForNode(node));
            });

            rootJSON["nodes"] = nodesJSONArray;

            // print out the created JSON
            QJsonDocument nodesDocument(rootJSON);

            // send the response
            connection->respond(HTTPConnection::StatusCode200, nodesDocument.toJson(), qPrintable(JSON_MIME_TYPE));

            return true;
        } else {
            // check if this is for json stats for a node
            const QString NODE_JSON_REGEX_STRING = QString("\\%1\\/(%2).json\\/?$").arg(URI_NODES).arg(UUID_REGEX_STRING);
            QRegExp nodeShowRegex(NODE_JSON_REGEX_STRING);

            if (nodeShowRegex.indexIn(url.path()) != -1) {
                QUuid matchingUUID = QUuid(nodeShowRegex.cap(1));

                // see if we have a node that matches this ID
                SharedNodePointer matchingNode = nodeList->nodeWithUUID(matchingUUID);
                if (matchingNode) {
                    // create a QJsonDocument with the stats QJsonObject
                    QJsonObject statsObject =
                        reinterpret_cast<DomainServerNodeData*>(matchingNode->getLinkedData())->getStatsJSONObject();

                    // add the node type to the JSON data for output purposes
                    statsObject["node_type"] = NodeType::getNodeTypeName(matchingNode->getType()).toLower().replace(' ', '-');

                    QJsonDocument statsDocument(statsObject);

                    // send the response
                    connection->respond(HTTPConnection::StatusCode200, statsDocument.toJson(), qPrintable(JSON_MIME_TYPE));

                    // tell the caller we processed the request
                    return true;
                }

                return false;
            }
        }
    } else if (connection->requestOperation() == QNetworkAccessManager::PostOperation) {
        if (url.path() == URI_ASSIGNMENT) {
            // this is a script upload - ask the HTTPConnection to parse the form data
            QList<FormData> formData = connection->parseFormData();

            // check optional headers for # of instances and pool
            const QString ASSIGNMENT_INSTANCES_HEADER = "ASSIGNMENT-INSTANCES";
            const QString ASSIGNMENT_POOL_HEADER = "ASSIGNMENT-POOL";

            QByteArray assignmentInstancesValue = connection->requestHeaders().value(ASSIGNMENT_INSTANCES_HEADER.toLocal8Bit());

            int numInstances = 1;

            if (!assignmentInstancesValue.isEmpty()) {
                // the user has requested a specific number of instances
                // so set that on the created assignment

                numInstances = assignmentInstancesValue.toInt();
            }

            QString assignmentPool = emptyPool;
            QByteArray assignmentPoolValue = connection->requestHeaders().value(ASSIGNMENT_POOL_HEADER.toLocal8Bit());

            if (!assignmentPoolValue.isEmpty()) {
                // specific pool requested, set that on the created assignment
                assignmentPool = QString(assignmentPoolValue);
            }


            for (int i = 0; i < numInstances; i++) {

                // create an assignment for this saved script
                Assignment* scriptAssignment = new Assignment(Assignment::CreateCommand, Assignment::AgentType, assignmentPool);

                QString newPath = pathForAssignmentScript(scriptAssignment->getUUID());

                // create a file with the GUID of the assignment in the script host location
                QFile scriptFile(newPath);
                if (scriptFile.open(QIODevice::WriteOnly)) {
                    scriptFile.write(formData[0].second);
                    
                    qDebug() << qPrintable(QString("Saved a script for assignment at %1%2")
                                           .arg(newPath).arg(assignmentPool == emptyPool ? "" : " - pool is " + assignmentPool));
                    
                    // add the script assigment to the assignment queue
                    SharedAssignmentPointer sharedScriptedAssignment(scriptAssignment);
                    _unfulfilledAssignments.enqueue(sharedScriptedAssignment);
                    _allAssignments.insert(sharedScriptedAssignment->getUUID(), sharedScriptedAssignment);
                } else {
                    // unable to save script for assignment - we shouldn't be here but debug it out
                    qDebug() << "Unable to save a script for assignment at" << newPath;
                    qDebug() << "Script will not be added to queue";
                }
            }

            // respond with a 200 code for successful upload
            connection->respond(HTTPConnection::StatusCode200);

            return true;
        }
    } else if (connection->requestOperation() == QNetworkAccessManager::DeleteOperation) {
        const QString ALL_NODE_DELETE_REGEX_STRING = QString("\\%1\\/?$").arg(URI_NODES);
        const QString NODE_DELETE_REGEX_STRING = QString("\\%1\\/(%2)\\/$").arg(URI_NODES).arg(UUID_REGEX_STRING);

        QRegExp allNodesDeleteRegex(ALL_NODE_DELETE_REGEX_STRING);
        QRegExp nodeDeleteRegex(NODE_DELETE_REGEX_STRING);

        if (nodeDeleteRegex.indexIn(url.path()) != -1) {
            // this is a request to DELETE one node by UUID

            // pull the captured string, if it exists
            QUuid deleteUUID = QUuid(nodeDeleteRegex.cap(1));

            SharedNodePointer nodeToKill = nodeList->nodeWithUUID(deleteUUID);

            if (nodeToKill) {
                // start with a 200 response
                connection->respond(HTTPConnection::StatusCode200);

                // we have a valid UUID and node - kill the node that has this assignment
                QMetaObject::invokeMethod(nodeList.data(), "killNodeWithUUID", Q_ARG(const QUuid&, deleteUUID));

                // successfully processed request
                return true;
            }

            return true;
        } else if (allNodesDeleteRegex.indexIn(url.path()) != -1) {
            qDebug() << "Received request to kill all nodes.";
            nodeList->eraseAllNodes();

            return true;
        }
    }

    // didn't process the request, let our DomainServerSettingsManager or HTTPManager handle
    return _settingsManager.handleAuthenticatedHTTPRequest(connection, url);
}

const QString HIFI_SESSION_COOKIE_KEY = "DS_WEB_SESSION_UUID";

bool DomainServer::handleHTTPSRequest(HTTPSConnection* connection, const QUrl &url, bool skipSubHandler) {
    const QString URI_OAUTH = "/oauth";
    qDebug() << "HTTPS request received at" << url.toString();
    if (url.path() == URI_OAUTH) {

        QUrlQuery codeURLQuery(url);

        const QString CODE_QUERY_KEY = "code";
        QString authorizationCode = codeURLQuery.queryItemValue(CODE_QUERY_KEY);

        const QString STATE_QUERY_KEY = "state";
        QUuid stateUUID = QUuid(codeURLQuery.queryItemValue(STATE_QUERY_KEY));

        if (!authorizationCode.isEmpty() && !stateUUID.isNull()) {
            // fire off a request with this code and state to get an access token for the user

            const QString OAUTH_TOKEN_REQUEST_PATH = "/oauth/token";
            QUrl tokenRequestUrl = _oauthProviderURL;
            tokenRequestUrl.setPath(OAUTH_TOKEN_REQUEST_PATH);

            const QString OAUTH_GRANT_TYPE_POST_STRING = "grant_type=authorization_code";
            QString tokenPostBody = OAUTH_GRANT_TYPE_POST_STRING;
            tokenPostBody += QString("&code=%1&redirect_uri=%2&client_id=%3&client_secret=%4")
                .arg(authorizationCode, oauthRedirectURL().toString(), _oauthClientID, _oauthClientSecret);

            QNetworkRequest tokenRequest(tokenRequestUrl);
            tokenRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
            
            QNetworkReply* tokenReply = NetworkAccessManager::getInstance().post(tokenRequest, tokenPostBody.toLocal8Bit());
            
            if (_webAuthenticationStateSet.remove(stateUUID)) {
                // this is a web user who wants to auth to access web interface
                // we hold the response back to them until we get their profile information
                // and can decide if they are let in or not
                
                QEventLoop loop;
                connect(tokenReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                
                // start the loop for the token request
                loop.exec();
                
                QNetworkReply* profileReply = profileRequestGivenTokenReply(tokenReply);
                
                // stop the loop once the profileReply is complete
                connect(profileReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                
                // restart the loop for the profile request
                loop.exec();
                
                // call helper method to get cookieHeaders
                Headers cookieHeaders = setupCookieHeadersFromProfileReply(profileReply);
                
                connection->respond(HTTPConnection::StatusCode302, QByteArray(),
                                    HTTPConnection::DefaultContentType, cookieHeaders);
                
                delete tokenReply;
                delete profileReply;
                
                // we've redirected the user back to our homepage
                return true;
                
            }
        }

        // respond with a 200 code indicating that login is complete
        connection->respond(HTTPConnection::StatusCode200);

        return true;
    } else {
        return false;
    }
}

bool DomainServer::isAuthenticatedRequest(HTTPConnection* connection, const QUrl& url) {
    
    const QByteArray HTTP_COOKIE_HEADER_KEY = "Cookie";
    const QString ADMIN_USERS_CONFIG_KEY = "admin-users";
    const QString ADMIN_ROLES_CONFIG_KEY = "admin-roles";
    const QString BASIC_AUTH_USERNAME_KEY_PATH = "security.http_username";
    const QString BASIC_AUTH_PASSWORD_KEY_PATH = "security.http_password";
    
    const QByteArray UNAUTHENTICATED_BODY = "You do not have permission to access this domain-server.";
    
    QVariantMap& settingsMap = _settingsManager.getSettingsMap();
    
    if (!_oauthProviderURL.isEmpty()
        && (settingsMap.contains(ADMIN_USERS_CONFIG_KEY) || settingsMap.contains(ADMIN_ROLES_CONFIG_KEY))) {
        QString cookieString = connection->requestHeaders().value(HTTP_COOKIE_HEADER_KEY);
        
        const QString COOKIE_UUID_REGEX_STRING = HIFI_SESSION_COOKIE_KEY + "=([\\d\\w-]+)($|;)";
        QRegExp cookieUUIDRegex(COOKIE_UUID_REGEX_STRING);
        
        QUuid cookieUUID;
        if (cookieString.indexOf(cookieUUIDRegex) != -1) {
            cookieUUID = cookieUUIDRegex.cap(1);
        }
        
        if (valueForKeyPath(settingsMap, BASIC_AUTH_USERNAME_KEY_PATH)) {
            qDebug() << "Config file contains web admin settings for OAuth and basic HTTP authentication."
                << "These cannot be combined - using OAuth for authentication.";
        }
        
        if (!cookieUUID.isNull() && _cookieSessionHash.contains(cookieUUID)) {
            // pull the QJSONObject for the user with this cookie UUID
            DomainServerWebSessionData sessionData = _cookieSessionHash.value(cookieUUID);
            QString profileUsername = sessionData.getUsername();
            
            if (settingsMap.value(ADMIN_USERS_CONFIG_KEY).toStringList().contains(profileUsername)) {
                // this is an authenticated user
                return true;
            }
            
            // loop the roles of this user and see if they are in the admin-roles array
            QStringList adminRolesArray = settingsMap.value(ADMIN_ROLES_CONFIG_KEY).toStringList();
            
            if (!adminRolesArray.isEmpty()) {
                foreach(const QString& userRole, sessionData.getRoles()) {
                    if (adminRolesArray.contains(userRole)) {
                        // this user has a role that allows them to administer the domain-server
                        return true;
                    }
                }
            }
            
            connection->respond(HTTPConnection::StatusCode401, UNAUTHENTICATED_BODY);
            
            // the user does not have allowed username or role, return 401
            return false;
        } else {
            // re-direct this user to OAuth page
            
            // generate a random state UUID to use
            QUuid stateUUID = QUuid::createUuid();
            
            // add it to the set so we can handle the callback from the OAuth provider
            _webAuthenticationStateSet.insert(stateUUID);
            
            QUrl oauthRedirectURL = oauthAuthorizationURL(stateUUID);
            
            Headers redirectHeaders;
            redirectHeaders.insert("Location", oauthRedirectURL.toEncoded());
            
            connection->respond(HTTPConnection::StatusCode302,
                                QByteArray(), HTTPConnection::DefaultContentType, redirectHeaders);
            
            // we don't know about this user yet, so they are not yet authenticated
            return false;
        }
    } else if (valueForKeyPath(settingsMap, BASIC_AUTH_USERNAME_KEY_PATH)) {
        // config file contains username and password combinations for basic auth
        const QByteArray BASIC_AUTH_HEADER_KEY = "Authorization";
        
        // check if a username and password have been provided with the request
        QString basicAuthString = connection->requestHeaders().value(BASIC_AUTH_HEADER_KEY);
        
        if (!basicAuthString.isEmpty()) {
            QStringList splitAuthString = basicAuthString.split(' ');
            QString base64String = splitAuthString.size() == 2 ? splitAuthString[1] : "";
            QString credentialString = QByteArray::fromBase64(base64String.toLocal8Bit());
            
            if (!credentialString.isEmpty()) {
                QStringList credentialList = credentialString.split(':');
                if (credentialList.size() == 2) {
                    QString headerUsername = credentialList[0];
                    QString headerPassword = credentialList[1];
                    
                    // we've pulled a username and password - now check if there is a match in our basic auth hash
                    QString settingsUsername = valueForKeyPath(settingsMap, BASIC_AUTH_USERNAME_KEY_PATH)->toString();
                    const QVariant* settingsPasswordVariant = valueForKeyPath(settingsMap, BASIC_AUTH_PASSWORD_KEY_PATH);
                    QString settingsPassword = settingsPasswordVariant ? settingsPasswordVariant->toString() : "";
                    
                    if (settingsUsername == headerUsername && headerPassword == settingsPassword) {
                        return true;
                    }
                }
            }
        }
        
        // basic HTTP auth being used but no username and password are present
        // or the username and password are not correct
        // send back a 401 and ask for basic auth
        
        const QByteArray HTTP_AUTH_REQUEST_HEADER_KEY = "WWW-Authenticate";
        static QString HTTP_AUTH_REALM_STRING = QString("Basic realm='%1 %2'")
            .arg(_hostname.isEmpty() ? "localhost" : _hostname)
            .arg("domain-server");
        
        Headers basicAuthHeader;
        basicAuthHeader.insert(HTTP_AUTH_REQUEST_HEADER_KEY, HTTP_AUTH_REALM_STRING.toUtf8());
        
        connection->respond(HTTPConnection::StatusCode401, UNAUTHENTICATED_BODY,
                            HTTPConnection::DefaultContentType, basicAuthHeader);
        
        // not authenticated, bubble up false
        return false;
        
    } else {
        // we don't have an OAuth URL + admin roles/usernames, so all users are authenticated
        return true;
    }
}

const QString OAUTH_JSON_ACCESS_TOKEN_KEY = "access_token";


QNetworkReply* DomainServer::profileRequestGivenTokenReply(QNetworkReply* tokenReply) {
    // pull the access token from the returned JSON and store it with the matching session UUID
    QJsonDocument returnedJSON = QJsonDocument::fromJson(tokenReply->readAll());
    QString accessToken = returnedJSON.object()[OAUTH_JSON_ACCESS_TOKEN_KEY].toString();
    
    // fire off a request to get this user's identity so we can see if we will let them in
    QUrl profileURL = _oauthProviderURL;
    profileURL.setPath("/api/v1/user/profile");
    profileURL.setQuery(QString("%1=%2").arg(OAUTH_JSON_ACCESS_TOKEN_KEY, accessToken));
    
    return NetworkAccessManager::getInstance().get(QNetworkRequest(profileURL));
}

const QString DS_SETTINGS_SESSIONS_GROUP = "web-sessions";

Headers DomainServer::setupCookieHeadersFromProfileReply(QNetworkReply* profileReply) {
    Headers cookieHeaders;
    
    // create a UUID for this cookie
    QUuid cookieUUID = QUuid::createUuid();
    
    QJsonDocument profileDocument = QJsonDocument::fromJson(profileReply->readAll());
    QJsonObject userObject = profileDocument.object()["data"].toObject()["user"].toObject();
    
    // add the profile to our in-memory data structure so we know who the user is when they send us their cookie
    DomainServerWebSessionData sessionData(userObject);
    _cookieSessionHash.insert(cookieUUID, sessionData);
    
    // persist the cookie to settings file so we can get it back on DS relaunch
    QStringList path = QStringList() << DS_SETTINGS_SESSIONS_GROUP << cookieUUID.toString();
    SettingHandles::SettingHandle<QVariant>(path).set(QVariant::fromValue(sessionData));
    
    // setup expiry for cookie to 1 month from today
    QDateTime cookieExpiry = QDateTime::currentDateTimeUtc().addMonths(1);
    
    QString cookieString = HIFI_SESSION_COOKIE_KEY + "=" + uuidStringWithoutCurlyBraces(cookieUUID.toString());
    cookieString += "; expires=" + cookieExpiry.toString("ddd, dd MMM yyyy HH:mm:ss") + " GMT";
    cookieString += "; domain=" + _hostname + "; path=/";
    
    cookieHeaders.insert("Set-Cookie", cookieString.toUtf8());
    
    // redirect the user back to the homepage so they can present their cookie and be authenticated
    QString redirectString = "http://" + _hostname + ":" + QString::number(_httpManager.serverPort());
    cookieHeaders.insert("Location", redirectString.toUtf8());
    
    return cookieHeaders;
}

void DomainServer::loadExistingSessionsFromSettings() {
    // read data for existing web sessions into memory so existing sessions can be leveraged
    Settings domainServerSettings;
    domainServerSettings.beginGroup(DS_SETTINGS_SESSIONS_GROUP);
    
    foreach(const QString& uuidKey, domainServerSettings.childKeys()) {
        _cookieSessionHash.insert(QUuid(uuidKey),
                                  domainServerSettings.value(uuidKey).value<DomainServerWebSessionData>());
        qDebug() << "Pulled web session from settings - cookie UUID is" << uuidKey;
    }
}

void DomainServer::refreshStaticAssignmentAndAddToQueue(SharedAssignmentPointer& assignment) {
    QUuid oldUUID = assignment->getUUID();
    assignment->resetUUID();

    qDebug() << "Reset UUID for assignment -" << *assignment.data() << "- and added to queue. Old UUID was"
        << uuidStringWithoutCurlyBraces(oldUUID);

    if (assignment->getType() == Assignment::AgentType && assignment->getPayload().isEmpty()) {
        // if this was an Agent without a script URL, we need to rename the old file so it can be retrieved at the new UUID
        QFile::rename(pathForAssignmentScript(oldUUID), pathForAssignmentScript(assignment->getUUID()));
    }

    // add the static assignment back under the right UUID, and to the queue
    _allAssignments.insert(assignment->getUUID(), assignment);
    _unfulfilledAssignments.enqueue(assignment);
}

void DomainServer::nodeAdded(SharedNodePointer node) {
    // we don't use updateNodeWithData, so add the DomainServerNodeData to the node here
    node->setLinkedData(new DomainServerNodeData());
}

void DomainServer::nodeKilled(SharedNodePointer node) {
    
    // remove this node from the connecting / connected ICE lists (if they exist)
    _connectingICEPeers.remove(node->getUUID());
    _connectedICEPeers.remove(node->getUUID());

    DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());

    if (nodeData) {
        // if this node's UUID matches a static assignment we need to throw it back in the assignment queue
        if (!nodeData->getAssignmentUUID().isNull()) {
            SharedAssignmentPointer matchedAssignment = _allAssignments.take(nodeData->getAssignmentUUID());

            if (matchedAssignment && matchedAssignment->isStatic()) {
                refreshStaticAssignmentAndAddToQueue(matchedAssignment);
            }
        }

        // cleanup the connection secrets that we set up for this node (on the other nodes)
        foreach (const QUuid& otherNodeSessionUUID, nodeData->getSessionSecretHash().keys()) {
            SharedNodePointer otherNode = DependencyManager::get<LimitedNodeList>()->nodeWithUUID(otherNodeSessionUUID);
            if (otherNode) {
                reinterpret_cast<DomainServerNodeData*>(otherNode->getLinkedData())->getSessionSecretHash().remove(node->getUUID());
            }
        }
    }
}

SharedAssignmentPointer DomainServer::matchingQueuedAssignmentForCheckIn(const QUuid& assignmentUUID, NodeType_t nodeType) {
    QQueue<SharedAssignmentPointer>::iterator i = _unfulfilledAssignments.begin();

    while (i != _unfulfilledAssignments.end()) {
        if (i->data()->getType() == Assignment::typeForNodeType(nodeType)
            && i->data()->getUUID() == assignmentUUID) {
            // we have an unfulfilled assignment to return

            // return the matching assignment
            return _unfulfilledAssignments.takeAt(i - _unfulfilledAssignments.begin());
        } else {
            ++i;
        }
    }

    return SharedAssignmentPointer();
}

SharedAssignmentPointer DomainServer::deployableAssignmentForRequest(const Assignment& requestAssignment) {
    // this is an unassigned client talking to us directly for an assignment
    // go through our queue and see if there are any assignments to give out
    QQueue<SharedAssignmentPointer>::iterator sharedAssignment = _unfulfilledAssignments.begin();

    while (sharedAssignment != _unfulfilledAssignments.end()) {
        Assignment* assignment = sharedAssignment->data();
        bool requestIsAllTypes = requestAssignment.getType() == Assignment::AllTypes;
        bool assignmentTypesMatch = assignment->getType() == requestAssignment.getType();
        bool nietherHasPool = assignment->getPool().isEmpty() && requestAssignment.getPool().isEmpty();
        bool assignmentPoolsMatch = assignment->getPool() == requestAssignment.getPool();

        if ((requestIsAllTypes || assignmentTypesMatch) && (nietherHasPool || assignmentPoolsMatch)) {

            // remove the assignment from the queue
            SharedAssignmentPointer deployableAssignment = _unfulfilledAssignments.takeAt(sharedAssignment
                                                                                          - _unfulfilledAssignments.begin());

            // until we get a connection for this assignment
            // put assignment back in queue but stick it at the back so the others have a chance to go out
            _unfulfilledAssignments.enqueue(deployableAssignment);

            // stop looping, we've handed out an assignment
            return deployableAssignment;
        } else {
            // push forward the iterator to check the next assignment
            ++sharedAssignment;
        }
    }

    return SharedAssignmentPointer();
}

void DomainServer::removeMatchingAssignmentFromQueue(const SharedAssignmentPointer& removableAssignment) {
    QQueue<SharedAssignmentPointer>::iterator potentialMatchingAssignment = _unfulfilledAssignments.begin();
    while (potentialMatchingAssignment != _unfulfilledAssignments.end()) {
        if (potentialMatchingAssignment->data()->getUUID() == removableAssignment->getUUID()) {
            _unfulfilledAssignments.erase(potentialMatchingAssignment);

            // we matched and removed an assignment, bail out
            break;
        } else {
            ++potentialMatchingAssignment;
        }
    }
}

void DomainServer::addStaticAssignmentsToQueue() {

    // if the domain-server has just restarted,
    // check if there are static assignments that we need to throw into the assignment queue
    QHash<QUuid, SharedAssignmentPointer> staticHashCopy = _allAssignments;
    QHash<QUuid, SharedAssignmentPointer>::iterator staticAssignment = staticHashCopy.begin();
    while (staticAssignment != staticHashCopy.end()) {
        // add any of the un-matched static assignments to the queue
        
        // enumerate the nodes and check if there is one with an attached assignment with matching UUID
        if (!DependencyManager::get<LimitedNodeList>()->nodeWithUUID(staticAssignment->data()->getUUID())) {
            // this assignment has not been fulfilled - reset the UUID and add it to the assignment queue
            refreshStaticAssignmentAndAddToQueue(*staticAssignment);
        }

        ++staticAssignment;
    }
}
