//
//  AssignmentClient.cpp
//  assignment-client/src
//
//  Created by Stephen Birarda on 11/25/2013.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QProcess>
#include <QSettings>
#include <QSharedMemory>
#include <QThread>
#include <QTimer>

#include <AccountManager.h>
#include <AddressManager.h>
#include <Assignment.h>
#include <HifiConfigVariantMap.h>
#include <LogHandler.h>
#include <LogUtils.h>
#include <NodeList.h>
#include <PacketHeaders.h>
#include <SharedUtil.h>
#include <ShutdownEventListener.h>
#include <SoundCache.h>

#include "AssignmentFactory.h"
#include "AssignmentThread.h"

#include "AssignmentClient.h"

const QString ASSIGNMENT_CLIENT_TARGET_NAME = "assignment-client";
const long long ASSIGNMENT_REQUEST_INTERVAL_MSECS = 1 * 1000;

SharedAssignmentPointer AssignmentClient::_currentAssignment;

int hifiSockAddrMeta = qRegisterMetaType<HifiSockAddr>("HifiSockAddr");

AssignmentClient::AssignmentClient(int &argc, char **argv) :
    QCoreApplication(argc, argv),
    _assignmentServerHostname(DEFAULT_ASSIGNMENT_SERVER_HOSTNAME),
    _localASPortSharedMem(NULL)
{
    LogUtils::init();

    setOrganizationName("High Fidelity");
    setOrganizationDomain("highfidelity.io");
    setApplicationName("assignment-client");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    
    // create a NodeList as an unassigned client
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    auto addressManager = DependencyManager::set<AddressManager>();
    auto nodeList = DependencyManager::set<NodeList>(NodeType::Unassigned);
    
    // setup a shutdown event listener to handle SIGTERM or WM_CLOSE for us
#ifdef _WIN32
    installNativeEventFilter(&ShutdownEventListener::getInstance());
#else
    ShutdownEventListener::getInstance();
#endif

    // set the logging target to the the CHILD_TARGET_NAME
    LogHandler::getInstance().setTargetName(ASSIGNMENT_CLIENT_TARGET_NAME);

    const QVariantMap argumentVariantMap = HifiConfigVariantMap::mergeCLParametersWithJSONConfig(arguments());

    const QString ASSIGNMENT_TYPE_OVERRIDE_OPTION = "t";
    const QString ASSIGNMENT_POOL_OPTION = "pool";
    const QString ASSIGNMENT_WALLET_DESTINATION_ID_OPTION = "wallet";
    const QString CUSTOM_ASSIGNMENT_SERVER_HOSTNAME_OPTION = "a";
    const QString CUSTOM_ASSIGNMENT_SERVER_PORT_OPTION = "p";

    Assignment::Type requestAssignmentType = Assignment::AllTypes;

    // check for an assignment type passed on the command line or in the config
    if (argumentVariantMap.contains(ASSIGNMENT_TYPE_OVERRIDE_OPTION)) {
        requestAssignmentType = (Assignment::Type) argumentVariantMap.value(ASSIGNMENT_TYPE_OVERRIDE_OPTION).toInt();
    }

    QString assignmentPool;

    // check for an assignment pool passed on the command line or in the config
    if (argumentVariantMap.contains(ASSIGNMENT_POOL_OPTION)) {
        assignmentPool = argumentVariantMap.value(ASSIGNMENT_POOL_OPTION).toString();
    }

    // setup our _requestAssignment member variable from the passed arguments
    _requestAssignment = Assignment(Assignment::RequestCommand, requestAssignmentType, assignmentPool);

    // check for a wallet UUID on the command line or in the config
    // this would represent where the user running AC wants funds sent to
    if (argumentVariantMap.contains(ASSIGNMENT_WALLET_DESTINATION_ID_OPTION)) {
        QUuid walletUUID = argumentVariantMap.value(ASSIGNMENT_WALLET_DESTINATION_ID_OPTION).toString();
        qDebug() << "The destination wallet UUID for credits is" << uuidStringWithoutCurlyBraces(walletUUID);
        _requestAssignment.setWalletUUID(walletUUID);
    }
    
    quint16 assignmentServerPort = DEFAULT_DOMAIN_SERVER_PORT;

    // check for an overriden assignment server hostname
    if (argumentVariantMap.contains(CUSTOM_ASSIGNMENT_SERVER_HOSTNAME_OPTION)) {
        // change the hostname for our assignment server
        _assignmentServerHostname = argumentVariantMap.value(CUSTOM_ASSIGNMENT_SERVER_HOSTNAME_OPTION).toString();
    }
    
    // check for an overriden assignment server port
    if (argumentVariantMap.contains(CUSTOM_ASSIGNMENT_SERVER_PORT_OPTION)) {
        assignmentServerPort =
        argumentVariantMap.value(CUSTOM_ASSIGNMENT_SERVER_PORT_OPTION).toString().toUInt();
    }
    
    _assignmentServerSocket = HifiSockAddr(_assignmentServerHostname, assignmentServerPort, true);
    nodeList->setAssignmentServerSocket(_assignmentServerSocket);

    qDebug() << "Assignment server socket is" << _assignmentServerSocket;
    
    // call a timer function every ASSIGNMENT_REQUEST_INTERVAL_MSECS to ask for assignment, if required
    qDebug() << "Waiting for assignment -" << _requestAssignment;

    QTimer* timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), SLOT(sendAssignmentRequest()));
    timer->start(ASSIGNMENT_REQUEST_INTERVAL_MSECS);

    // connect our readPendingDatagrams method to the readyRead() signal of the socket
    connect(&nodeList->getNodeSocket(), &QUdpSocket::readyRead, this, &AssignmentClient::readPendingDatagrams);

    // connections to AccountManager for authentication
    connect(&AccountManager::getInstance(), &AccountManager::authRequired,
            this, &AssignmentClient::handleAuthenticationRequest);
    
    // Create Singleton objects on main thread
    NetworkAccessManager::getInstance();
    SoundCache::getInstance();
}

void AssignmentClient::sendAssignmentRequest() {
    if (!_currentAssignment) {
        
        auto nodeList = DependencyManager::get<NodeList>();
        
        if (_assignmentServerHostname == "localhost") {
            // we want to check again for the local domain-server port in case the DS has restarted
            if (!_localASPortSharedMem) {
                _localASPortSharedMem = new QSharedMemory(DOMAIN_SERVER_LOCAL_PORT_SMEM_KEY, this);
                
                if (!_localASPortSharedMem->attach(QSharedMemory::ReadOnly)) {
                    qWarning() << "Could not attach to shared memory at key" << DOMAIN_SERVER_LOCAL_PORT_SMEM_KEY
                        << "- will attempt to connect to domain-server on" << _assignmentServerSocket.getPort();
                }
            }
            
            if (_localASPortSharedMem->isAttached()) {
                _localASPortSharedMem->lock();
                
                quint16 localAssignmentServerPort;
                memcpy(&localAssignmentServerPort, _localASPortSharedMem->data(), sizeof(localAssignmentServerPort));
                
                _localASPortSharedMem->unlock();
                
                if (localAssignmentServerPort != _assignmentServerSocket.getPort()) {
                    qDebug() << "Port for local assignment server read from shared memory is"
                        << localAssignmentServerPort;
                    
                    _assignmentServerSocket.setPort(localAssignmentServerPort);
                    nodeList->setAssignmentServerSocket(_assignmentServerSocket);
                }
            }
            
        }
        
        nodeList->sendAssignment(_requestAssignment);
    }
}

void AssignmentClient::readPendingDatagrams() {
    auto nodeList = DependencyManager::get<NodeList>();

    QByteArray receivedPacket;
    HifiSockAddr senderSockAddr;

    while (nodeList->getNodeSocket().hasPendingDatagrams()) {
        receivedPacket.resize(nodeList->getNodeSocket().pendingDatagramSize());
        nodeList->getNodeSocket().readDatagram(receivedPacket.data(), receivedPacket.size(),
                                               senderSockAddr.getAddressPointer(), senderSockAddr.getPortPointer());

        if (nodeList->packetVersionAndHashMatch(receivedPacket)) {
            if (packetTypeForPacket(receivedPacket) == PacketTypeCreateAssignment) {
                // construct the deployed assignment from the packet data
                _currentAssignment = SharedAssignmentPointer(AssignmentFactory::unpackAssignment(receivedPacket));

                if (_currentAssignment) {
                    qDebug() << "Received an assignment -" << *_currentAssignment;

                    // switch our DomainHandler hostname and port to whoever sent us the assignment

                    nodeList->getDomainHandler().setSockAddr(senderSockAddr, _assignmentServerHostname);
                    nodeList->getDomainHandler().setAssignmentUUID(_currentAssignment->getUUID());

                    qDebug() << "Destination IP for assignment is" << nodeList->getDomainHandler().getIP().toString();

                    // start the deployed assignment
                    AssignmentThread* workerThread = new AssignmentThread(_currentAssignment, this);

                    connect(workerThread, &QThread::started, _currentAssignment.data(), &ThreadedAssignment::run);
                    connect(_currentAssignment.data(), &ThreadedAssignment::finished, workerThread, &QThread::quit);
                    connect(_currentAssignment.data(), &ThreadedAssignment::finished,
                            this, &AssignmentClient::assignmentCompleted);
                    connect(workerThread, &QThread::finished, workerThread, &QThread::deleteLater);

                    _currentAssignment->moveToThread(workerThread);

                    // move the NodeList to the thread used for the _current assignment
                    nodeList->moveToThread(workerThread);

                    // let the assignment handle the incoming datagrams for its duration
                    disconnect(&nodeList->getNodeSocket(), 0, this, 0);
                    connect(&nodeList->getNodeSocket(), &QUdpSocket::readyRead, _currentAssignment.data(),
                            &ThreadedAssignment::readPendingDatagrams);

                    // Starts an event loop, and emits workerThread->started()
                    workerThread->start();
                } else {
                    qDebug() << "Received an assignment that could not be unpacked. Re-requesting.";
                }
            } else {
                // have the NodeList attempt to handle it
                nodeList->processNodeData(senderSockAddr, receivedPacket);
            }
        }
    }
}

void AssignmentClient::handleAuthenticationRequest() {
    const QString DATA_SERVER_USERNAME_ENV = "HIFI_AC_USERNAME";
    const QString DATA_SERVER_PASSWORD_ENV = "HIFI_AC_PASSWORD";

    // this node will be using an authentication server, let's make sure we have a username/password
    QProcessEnvironment sysEnvironment = QProcessEnvironment::systemEnvironment();

    QString username = sysEnvironment.value(DATA_SERVER_USERNAME_ENV);
    QString password = sysEnvironment.value(DATA_SERVER_PASSWORD_ENV);

    AccountManager& accountManager = AccountManager::getInstance();

    if (!username.isEmpty() && !password.isEmpty()) {
        // ask the account manager to log us in from the env variables
        accountManager.requestAccessToken(username, password);
    } else {
        qDebug() << "Authentication was requested against" << qPrintable(accountManager.getAuthURL().toString())
            << "but both or one of" << qPrintable(DATA_SERVER_USERNAME_ENV)
            << "/" << qPrintable(DATA_SERVER_PASSWORD_ENV) << "are not set. Unable to authenticate.";

        return;
    }
}

void AssignmentClient::assignmentCompleted() {
    // reset the logging target to the the CHILD_TARGET_NAME
    LogHandler::getInstance().setTargetName(ASSIGNMENT_CLIENT_TARGET_NAME);

    qDebug("Assignment finished or never started - waiting for new assignment.");

    auto nodeList = DependencyManager::get<NodeList>();

    // have us handle incoming NodeList datagrams again
    disconnect(&nodeList->getNodeSocket(), 0, _currentAssignment.data(), 0);
    connect(&nodeList->getNodeSocket(), &QUdpSocket::readyRead, this, &AssignmentClient::readPendingDatagrams);

    // clear our current assignment shared pointer now that we're done with it
    // if the assignment thread is still around it has its own shared pointer to the assignment
    _currentAssignment.clear();

    // reset our NodeList by switching back to unassigned and clearing the list
    nodeList->setOwnerType(NodeType::Unassigned);
    nodeList->reset();
    nodeList->resetNodeInterestSet();
}
