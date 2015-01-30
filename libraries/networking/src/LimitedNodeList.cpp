//
//  LimitedNodeList.cpp
//  libraries/networking/src
//
//  Created by Stephen Birarda on 2/15/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <cstring>
#include <cstdlib>
#include <cstdio>

#include <QtCore/QDataStream>
#include <QtCore/QDebug>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>
#include <QtNetwork/QHostInfo>

#include <LogHandler.h>

#include <tbb/parallel_for.h>

#include "AccountManager.h"
#include "Assignment.h"
#include "HifiSockAddr.h"
#include "LimitedNodeList.h"
#include "PacketHeaders.h"
#include "SharedUtil.h"
#include "UUID.h"

const char SOLO_NODE_TYPES[2] = {
    NodeType::AvatarMixer,
    NodeType::AudioMixer
};

const QUrl DEFAULT_NODE_AUTH_URL = QUrl("https://data.highfidelity.io");

LimitedNodeList::LimitedNodeList(unsigned short socketListenPort, unsigned short dtlsListenPort) :
    _sessionUUID(),
    _nodeHash(),
    _nodeMutex(QReadWriteLock::Recursive),
    _nodeSocket(this),
    _dtlsSocket(NULL),
    _localSockAddr(),
    _publicSockAddr(),
    _stunSockAddr(STUN_SERVER_HOSTNAME, STUN_SERVER_PORT),
    _numCollectedPackets(0),
    _numCollectedBytes(0),
    _packetStatTimer()
{
    static bool firstCall = true;
    if (firstCall) {
        NodeType::init();
        
        // register the SharedNodePointer meta-type for signals/slots
        qRegisterMetaType<SharedNodePointer>();
        firstCall = false;
    }
    
    _nodeSocket.bind(QHostAddress::AnyIPv4, socketListenPort);
    qDebug() << "NodeList socket is listening on" << _nodeSocket.localPort();
    
    if (dtlsListenPort > 0) {
        // only create the DTLS socket during constructor if a custom port is passed
        _dtlsSocket = new QUdpSocket(this);
        
        _dtlsSocket->bind(QHostAddress::AnyIPv4, dtlsListenPort);
        qDebug() << "NodeList DTLS socket is listening on" << _dtlsSocket->localPort();
    }
    
    const int LARGER_BUFFER_SIZE = 1048576;
    changeSocketBufferSizes(LARGER_BUFFER_SIZE);
    
    // check for local socket updates every so often
    const int LOCAL_SOCKET_UPDATE_INTERVAL_MSECS = 5 * 1000;
    QTimer* localSocketUpdate = new QTimer(this);
    connect(localSocketUpdate, &QTimer::timeout, this, &LimitedNodeList::updateLocalSockAddr);
    localSocketUpdate->start(LOCAL_SOCKET_UPDATE_INTERVAL_MSECS);
    
    // check the local socket right now
    updateLocalSockAddr();
    
    _packetStatTimer.start();
}

void LimitedNodeList::setSessionUUID(const QUuid& sessionUUID) {
    QUuid oldUUID = _sessionUUID;
    _sessionUUID = sessionUUID;
    
    if (sessionUUID != oldUUID) {
        qDebug() << "NodeList UUID changed from" <<  uuidStringWithoutCurlyBraces(oldUUID)
        << "to" << uuidStringWithoutCurlyBraces(_sessionUUID);
        emit uuidChanged(sessionUUID, oldUUID);
    }
}

QUdpSocket& LimitedNodeList::getDTLSSocket() {
    if (!_dtlsSocket) {
        // DTLS socket getter called but no DTLS socket exists, create it now
        _dtlsSocket = new QUdpSocket(this);
        
        _dtlsSocket->bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::DontShareAddress);
        
        // we're using DTLS and our socket is good to go, so make the required DTLS changes
        // DTLS requires that IP_DONTFRAG be set
        // This is not accessible on some platforms (OS X) so we need to make sure DTLS still works without it
        
#if defined(IP_DONTFRAG) || defined(IP_MTU_DISCOVER)
        qDebug() << "Making required DTLS changes to LimitedNodeList DTLS socket.";
        
        int socketHandle = _dtlsSocket->socketDescriptor();
#if defined(IP_DONTFRAG)
        int optValue = 1;
        setsockopt(socketHandle, IPPROTO_IP, IP_DONTFRAG, reinterpret_cast<const void*>(&optValue), sizeof(optValue));
#elif defined(IP_MTU_DISCOVER)
        int optValue = 1;
        setsockopt(socketHandle, IPPROTO_IP, IP_MTU_DISCOVER, reinterpret_cast<const void*>(&optValue), sizeof(optValue));
#endif
#endif
        
        qDebug() << "LimitedNodeList DTLS socket is listening on" << _dtlsSocket->localPort();
    }
    
    return *_dtlsSocket;
}

void LimitedNodeList::changeSocketBufferSizes(int numBytes) {
    for (int i = 0; i < 2; i++) {
        QAbstractSocket::SocketOption bufferOpt;
        QString bufferTypeString;
        if (i == 0) {
            bufferOpt = QAbstractSocket::SendBufferSizeSocketOption;
            bufferTypeString = "send";
            
        } else {
            bufferOpt = QAbstractSocket::ReceiveBufferSizeSocketOption;
            bufferTypeString = "receive";
        }
        int oldBufferSize = _nodeSocket.socketOption(bufferOpt).toInt();
        if (oldBufferSize < numBytes) {
            _nodeSocket.setSocketOption(bufferOpt, numBytes);
            int newBufferSize = _nodeSocket.socketOption(bufferOpt).toInt();
            
            qDebug() << "Changed socket" << bufferTypeString << "buffer size from" << oldBufferSize << "to"
                << newBufferSize << "bytes";
        } else {
            // don't make the buffer smaller
            qDebug() << "Did not change socket" << bufferTypeString << "buffer size from" << oldBufferSize
                << "since it is larger than desired size of" << numBytes;
        }
    }
}

bool LimitedNodeList::packetVersionAndHashMatch(const QByteArray& packet) {
    PacketType checkType = packetTypeForPacket(packet);
    int numPacketTypeBytes = numBytesArithmeticCodingFromBuffer(packet.data());
    
    if (packet[numPacketTypeBytes] != versionForPacketType(checkType)
        && checkType != PacketTypeStunResponse) {
        PacketType mismatchType = packetTypeForPacket(packet);
        
        static QMultiMap<QUuid, PacketType> versionDebugSuppressMap;
        
        QUuid senderUUID = uuidFromPacketHeader(packet);
        if (!versionDebugSuppressMap.contains(senderUUID, checkType)) {
            qDebug() << "Packet version mismatch on" << packetTypeForPacket(packet) << "- Sender"
            << uuidFromPacketHeader(packet) << "sent" << qPrintable(QString::number(packet[numPacketTypeBytes])) << "but"
            << qPrintable(QString::number(versionForPacketType(mismatchType))) << "expected.";
            
            versionDebugSuppressMap.insert(senderUUID, checkType);
        }
        
        return false;
    }
    
    if (!NON_VERIFIED_PACKETS.contains(checkType)) {
        // figure out which node this is from
        SharedNodePointer sendingNode = sendingNodeForPacket(packet);
        if (sendingNode) {
            // check if the md5 hash in the header matches the hash we would expect
            if (hashFromPacketHeader(packet) == hashForPacketAndConnectionUUID(packet, sendingNode->getConnectionSecret())) {
                return true;
            } else {
                static QMultiMap<QUuid, PacketType> hashDebugSuppressMap;
                
                QUuid senderUUID = uuidFromPacketHeader(packet);
                if (!hashDebugSuppressMap.contains(senderUUID, checkType)) {
                    qDebug() << "Packet hash mismatch on" << checkType << "- Sender"
                    << uuidFromPacketHeader(packet);
                    
                    hashDebugSuppressMap.insert(senderUUID, checkType);
                }
            }
        } else {
            static QString repeatedMessage
                = LogHandler::getInstance().addRepeatedMessageRegex("Packet of type \\d+ received from unknown node with UUID");
            
            qDebug() << "Packet of type" << checkType << "received from unknown node with UUID"
                << qPrintable(uuidStringWithoutCurlyBraces(uuidFromPacketHeader(packet)));
        }
    } else {
        return true;
    }
    
    return false;
}

qint64 LimitedNodeList::writeDatagram(const QByteArray& datagram, const HifiSockAddr& destinationSockAddr,
                                      const QUuid& connectionSecret) {
    QByteArray datagramCopy = datagram;
    
    if (!connectionSecret.isNull()) {
        // setup the MD5 hash for source verification in the header
        replaceHashInPacketGivenConnectionUUID(datagramCopy, connectionSecret);
    }
    
    // stat collection for packets
    ++_numCollectedPackets;
    _numCollectedBytes += datagram.size();
    
    qint64 bytesWritten = _nodeSocket.writeDatagram(datagramCopy,
                                                    destinationSockAddr.getAddress(), destinationSockAddr.getPort());
    
    if (bytesWritten < 0) {
        qDebug() << "ERROR in writeDatagram:" << _nodeSocket.error() << "-" << _nodeSocket.errorString();
    }
    
    return bytesWritten;
}

qint64 LimitedNodeList::writeDatagram(const QByteArray& datagram, const SharedNodePointer& destinationNode,
                               const HifiSockAddr& overridenSockAddr) {
    if (destinationNode) {
        // if we don't have an ovveriden address, assume they want to send to the node's active socket
        const HifiSockAddr* destinationSockAddr = &overridenSockAddr;
        if (overridenSockAddr.isNull()) {
            if (destinationNode->getActiveSocket()) {
                // use the node's active socket as the destination socket
                destinationSockAddr = destinationNode->getActiveSocket();
            } else {
                // we don't have a socket to send to, return 0
                return 0;
            }
        }
        
        return writeDatagram(datagram, *destinationSockAddr, destinationNode->getConnectionSecret());
    }
    
    // didn't have a destinationNode to send to, return 0
    return 0;
}

qint64 LimitedNodeList::writeUnverifiedDatagram(const QByteArray& datagram, const SharedNodePointer& destinationNode,
                               const HifiSockAddr& overridenSockAddr) {
    if (destinationNode) {
        // if we don't have an ovveriden address, assume they want to send to the node's active socket
        const HifiSockAddr* destinationSockAddr = &overridenSockAddr;
        if (overridenSockAddr.isNull()) {
            if (destinationNode->getActiveSocket()) {
                // use the node's active socket as the destination socket
                destinationSockAddr = destinationNode->getActiveSocket();
            } else {
                // we don't have a socket to send to, return 0
                return 0;
            }
        }
        
        // don't use the node secret!
        return writeDatagram(datagram, *destinationSockAddr, QUuid());
    }
    
    // didn't have a destinationNode to send to, return 0
    return 0;
}

qint64 LimitedNodeList::writeUnverifiedDatagram(const QByteArray& datagram, const HifiSockAddr& destinationSockAddr) {
    return writeDatagram(datagram, destinationSockAddr, QUuid());
}

qint64 LimitedNodeList::writeDatagram(const char* data, qint64 size, const SharedNodePointer& destinationNode,
                               const HifiSockAddr& overridenSockAddr) {
    return writeDatagram(QByteArray(data, size), destinationNode, overridenSockAddr);
}

qint64 LimitedNodeList::writeUnverifiedDatagram(const char* data, qint64 size, const SharedNodePointer& destinationNode,
                               const HifiSockAddr& overridenSockAddr) {
    return writeUnverifiedDatagram(QByteArray(data, size), destinationNode, overridenSockAddr);
}

void LimitedNodeList::processNodeData(const HifiSockAddr& senderSockAddr, const QByteArray& packet) {
    // the node decided not to do anything with this packet
    // if it comes from a known source we should keep that node alive
    SharedNodePointer matchingNode = sendingNodeForPacket(packet);
    if (matchingNode) {
        matchingNode->setLastHeardMicrostamp(usecTimestampNow());
    }
}

int LimitedNodeList::updateNodeWithDataFromPacket(const SharedNodePointer& matchingNode, const QByteArray &packet) {
    QMutexLocker locker(&matchingNode->getMutex());
    
    matchingNode->setLastHeardMicrostamp(usecTimestampNow());
    matchingNode->recordBytesReceived(packet.size());
    
    if (!matchingNode->getLinkedData() && linkedDataCreateCallback) {
        linkedDataCreateCallback(matchingNode.data());
    }
    
    QMutexLocker linkedDataLocker(&matchingNode->getLinkedData()->getMutex());
    
    return matchingNode->getLinkedData()->parseData(packet);
}

int LimitedNodeList::findNodeAndUpdateWithDataFromPacket(const QByteArray& packet) {
    SharedNodePointer matchingNode = sendingNodeForPacket(packet);
    
    if (matchingNode) {
        updateNodeWithDataFromPacket(matchingNode, packet);
    }
    
    // we weren't able to match the sender address to the address we have for this node, unlock and don't parse
    return 0;
}

SharedNodePointer LimitedNodeList::nodeWithUUID(const QUuid& nodeUUID) {
    QReadLocker readLocker(&_nodeMutex);
    
    NodeHash::const_iterator it = _nodeHash.find(nodeUUID);
    return it == _nodeHash.cend() ? SharedNodePointer() : it->second;
 }

SharedNodePointer LimitedNodeList::sendingNodeForPacket(const QByteArray& packet) {
    QUuid nodeUUID = uuidFromPacketHeader(packet);
    
    // return the matching node, or NULL if there is no match
    return nodeWithUUID(nodeUUID);
}

void LimitedNodeList::eraseAllNodes() {
    qDebug() << "Clearing the NodeList. Deleting all nodes in list.";
    
    QSet<SharedNodePointer> killedNodes;
    eachNode([&killedNodes](const SharedNodePointer& node){
        killedNodes.insert(node);
    });
    
    // iterate the current nodes, emit that they are dying and remove them from the hash
    _nodeMutex.lockForWrite();
    _nodeHash.clear();
    _nodeMutex.unlock();
    
    foreach(const SharedNodePointer& killedNode, killedNodes) {
        handleNodeKill(killedNode);
    }
}

void LimitedNodeList::reset() {
    eraseAllNodes();
}

void LimitedNodeList::killNodeWithUUID(const QUuid& nodeUUID) {
    _nodeMutex.lockForRead();
    
    NodeHash::iterator it = _nodeHash.find(nodeUUID);
    if (it != _nodeHash.end()) {
        SharedNodePointer matchingNode = it->second;
        
        _nodeMutex.unlock();
        
        QWriteLocker writeLocker(&_nodeMutex);
        _nodeHash.unsafe_erase(it);
        
        handleNodeKill(matchingNode);
    } else {
        _nodeMutex.unlock();
    }
}

void LimitedNodeList::processKillNode(const QByteArray& dataByteArray) {
    // read the node id
    QUuid nodeUUID = QUuid::fromRfc4122(dataByteArray.mid(numBytesForPacketHeader(dataByteArray), NUM_BYTES_RFC4122_UUID));

    // kill the node with this UUID, if it exists
    killNodeWithUUID(nodeUUID);
}

void LimitedNodeList::handleNodeKill(const SharedNodePointer& node) {
    qDebug() << "Killed" << *node;
    emit nodeKilled(node);
}

SharedNodePointer LimitedNodeList::addOrUpdateNode(const QUuid& uuid, NodeType_t nodeType,
                                                   const HifiSockAddr& publicSocket, const HifiSockAddr& localSocket) {
    try {
        SharedNodePointer matchingNode = _nodeHash.at(uuid);
        
        matchingNode->setPublicSocket(publicSocket);
        matchingNode->setLocalSocket(localSocket);
        
        return matchingNode;
    } catch (std::out_of_range) {
        // we didn't have this node, so add them
        Node* newNode = new Node(uuid, nodeType, publicSocket, localSocket);
        SharedNodePointer newNodeSharedPointer(newNode, &QObject::deleteLater);
        
        _nodeHash.insert(UUIDNodePair(newNode->getUUID(), newNodeSharedPointer));
        
        qDebug() << "Added" << *newNode;
        
        emit nodeAdded(newNodeSharedPointer);
        
        return newNodeSharedPointer;
    }
}

unsigned LimitedNodeList::broadcastToNodes(const QByteArray& packet, const NodeSet& destinationNodeTypes) {
    unsigned n = 0;
    
    eachNode([&](const SharedNodePointer& node){
        if (destinationNodeTypes.contains(node->getType())) {
            writeDatagram(packet, node);
            ++n;
        }
    });

    return n;
}

QByteArray LimitedNodeList::constructPingPacket(PingType_t pingType, bool isVerified, const QUuid& packetHeaderID) {
    QByteArray pingPacket = byteArrayWithPopulatedHeader(isVerified ? PacketTypePing : PacketTypeUnverifiedPing,
                                                         packetHeaderID);
    
    QDataStream packetStream(&pingPacket, QIODevice::Append);
    
    packetStream << pingType;
    packetStream << usecTimestampNow();
    
    return pingPacket;
}

QByteArray LimitedNodeList::constructPingReplyPacket(const QByteArray& pingPacket, const QUuid& packetHeaderID) {
    QDataStream pingPacketStream(pingPacket);
    pingPacketStream.skipRawData(numBytesForPacketHeader(pingPacket));
    
    PingType_t typeFromOriginalPing;
    pingPacketStream >> typeFromOriginalPing;
    
    quint64 timeFromOriginalPing;
    pingPacketStream >> timeFromOriginalPing;
    
    PacketType replyType = (packetTypeForPacket(pingPacket) == PacketTypePing)
        ? PacketTypePingReply : PacketTypeUnverifiedPingReply;
    
    QByteArray replyPacket = byteArrayWithPopulatedHeader(replyType, packetHeaderID);
    QDataStream packetStream(&replyPacket, QIODevice::Append);
    
    packetStream << typeFromOriginalPing << timeFromOriginalPing << usecTimestampNow();
    
    return replyPacket;
}

SharedNodePointer LimitedNodeList::soloNodeOfType(char nodeType) {
    return nodeMatchingPredicate([&](const SharedNodePointer& node){
        return node->getType() == nodeType;
    });
}

void LimitedNodeList::getPacketStats(float& packetsPerSecond, float& bytesPerSecond) {
    packetsPerSecond = (float) _numCollectedPackets / ((float) _packetStatTimer.elapsed() / 1000.0f);
    bytesPerSecond = (float) _numCollectedBytes / ((float) _packetStatTimer.elapsed() / 1000.0f);
}

void LimitedNodeList::resetPacketStats() {
    _numCollectedPackets = 0;
    _numCollectedBytes = 0;
    _packetStatTimer.restart();
}

void LimitedNodeList::removeSilentNodes() {
    QSet<SharedNodePointer> killedNodes;
    
    eachNodeHashIterator([&](NodeHash::iterator& it){
        SharedNodePointer node = it->second;
        node->getMutex().lock();
        
        if ((usecTimestampNow() - node->getLastHeardMicrostamp()) > (NODE_SILENCE_THRESHOLD_MSECS * USECS_PER_MSEC)) {
            // call the NodeHash erase to get rid of this node
            it = _nodeHash.unsafe_erase(it);
            
            killedNodes.insert(node);
        } else {
            // we didn't erase this node, push the iterator forwards
            ++it;
        }
        
        node->getMutex().unlock();
    });
    
    foreach(const SharedNodePointer& killedNode, killedNodes) {
        handleNodeKill(killedNode);
    }
}

const uint32_t RFC_5389_MAGIC_COOKIE = 0x2112A442;
const int NUM_BYTES_STUN_HEADER = 20;

void LimitedNodeList::sendSTUNRequest() {
    
    unsigned char stunRequestPacket[NUM_BYTES_STUN_HEADER];
    
    int packetIndex = 0;
    
    const uint32_t RFC_5389_MAGIC_COOKIE_NETWORK_ORDER = htonl(RFC_5389_MAGIC_COOKIE);
    
    // leading zeros + message type
    const uint16_t REQUEST_MESSAGE_TYPE = htons(0x0001);
    memcpy(stunRequestPacket + packetIndex, &REQUEST_MESSAGE_TYPE, sizeof(REQUEST_MESSAGE_TYPE));
    packetIndex += sizeof(REQUEST_MESSAGE_TYPE);
    
    // message length (no additional attributes are included)
    uint16_t messageLength = 0;
    memcpy(stunRequestPacket + packetIndex, &messageLength, sizeof(messageLength));
    packetIndex += sizeof(messageLength);
    
    memcpy(stunRequestPacket + packetIndex, &RFC_5389_MAGIC_COOKIE_NETWORK_ORDER, sizeof(RFC_5389_MAGIC_COOKIE_NETWORK_ORDER));
    packetIndex += sizeof(RFC_5389_MAGIC_COOKIE_NETWORK_ORDER);
    
    // transaction ID (random 12-byte unsigned integer)
    const uint NUM_TRANSACTION_ID_BYTES = 12;
    QUuid randomUUID = QUuid::createUuid();
    memcpy(stunRequestPacket + packetIndex, randomUUID.toRfc4122().data(), NUM_TRANSACTION_ID_BYTES);
    
    _nodeSocket.writeDatagram((char*) stunRequestPacket, sizeof(stunRequestPacket),
                              _stunSockAddr.getAddress(), _stunSockAddr.getPort());
}

void LimitedNodeList::rebindNodeSocket() {
    quint16 oldPort = _nodeSocket.localPort();
    
    _nodeSocket.close();
    _nodeSocket.bind(QHostAddress::AnyIPv4, oldPort);
}

bool LimitedNodeList::processSTUNResponse(const QByteArray& packet) {
    // check the cookie to make sure this is actually a STUN response
    // and read the first attribute and make sure it is a XOR_MAPPED_ADDRESS
    const int NUM_BYTES_MESSAGE_TYPE_AND_LENGTH = 4;
    const uint16_t XOR_MAPPED_ADDRESS_TYPE = htons(0x0020);
    
    const uint32_t RFC_5389_MAGIC_COOKIE_NETWORK_ORDER = htonl(RFC_5389_MAGIC_COOKIE);
    
    int attributeStartIndex = NUM_BYTES_STUN_HEADER;
    
    if (memcmp(packet.data() + NUM_BYTES_MESSAGE_TYPE_AND_LENGTH,
               &RFC_5389_MAGIC_COOKIE_NETWORK_ORDER,
               sizeof(RFC_5389_MAGIC_COOKIE_NETWORK_ORDER)) == 0) {
        
        // enumerate the attributes to find XOR_MAPPED_ADDRESS_TYPE
        while (attributeStartIndex < packet.size()) {
            if (memcmp(packet.data() + attributeStartIndex, &XOR_MAPPED_ADDRESS_TYPE, sizeof(XOR_MAPPED_ADDRESS_TYPE)) == 0) {
                const int NUM_BYTES_STUN_ATTR_TYPE_AND_LENGTH = 4;
                const int NUM_BYTES_FAMILY_ALIGN = 1;
                const uint8_t IPV4_FAMILY_NETWORK_ORDER = htons(0x01) >> 8;
                
                
                
                int byteIndex = attributeStartIndex + NUM_BYTES_STUN_ATTR_TYPE_AND_LENGTH + NUM_BYTES_FAMILY_ALIGN;
                
                uint8_t addressFamily = 0;
                memcpy(&addressFamily, packet.data() + byteIndex, sizeof(addressFamily));
                
                byteIndex += sizeof(addressFamily);
                
                if (addressFamily == IPV4_FAMILY_NETWORK_ORDER) {
                    // grab the X-Port
                    uint16_t xorMappedPort = 0;
                    memcpy(&xorMappedPort, packet.data() + byteIndex, sizeof(xorMappedPort));
                    
                    uint16_t newPublicPort = ntohs(xorMappedPort) ^ (ntohl(RFC_5389_MAGIC_COOKIE_NETWORK_ORDER) >> 16);
                    
                    byteIndex += sizeof(xorMappedPort);
                    
                    // grab the X-Address
                    uint32_t xorMappedAddress = 0;
                    memcpy(&xorMappedAddress, packet.data() + byteIndex, sizeof(xorMappedAddress));
                    
                    uint32_t stunAddress = ntohl(xorMappedAddress) ^ ntohl(RFC_5389_MAGIC_COOKIE_NETWORK_ORDER);
                    
                    QHostAddress newPublicAddress = QHostAddress(stunAddress);
                    
                    if (newPublicAddress != _publicSockAddr.getAddress() || newPublicPort != _publicSockAddr.getPort()) {
                        _publicSockAddr = HifiSockAddr(newPublicAddress, newPublicPort);
                        
                        qDebug("New public socket received from STUN server is %s:%hu",
                               _publicSockAddr.getAddress().toString().toLocal8Bit().constData(),
                               _publicSockAddr.getPort());
                        
                        emit publicSockAddrChanged(_publicSockAddr);                        
                    }
                    
                    return true;
                }
            } else {
                // push forward attributeStartIndex by the length of this attribute
                const int NUM_BYTES_ATTRIBUTE_TYPE = 2;
                
                uint16_t attributeLength = 0;
                memcpy(&attributeLength, packet.data() + attributeStartIndex + NUM_BYTES_ATTRIBUTE_TYPE,
                       sizeof(attributeLength));
                attributeLength = ntohs(attributeLength);
                
                attributeStartIndex += NUM_BYTES_MESSAGE_TYPE_AND_LENGTH + attributeLength;
            }
        }
    }
    
    return false;
}

void LimitedNodeList::updateLocalSockAddr() {
    HifiSockAddr newSockAddr(getLocalAddress(), _nodeSocket.localPort());
    if (newSockAddr != _localSockAddr) {
        
        if (_localSockAddr.isNull()) {
            qDebug() << "Local socket is" << newSockAddr;
        } else {
            qDebug() << "Local socket has changed from" << _localSockAddr << "to" << newSockAddr;
        }
        
        _localSockAddr = newSockAddr;
        
        emit localSockAddrChanged(_localSockAddr);
    }
}

void LimitedNodeList::sendHeartbeatToIceServer(const HifiSockAddr& iceServerSockAddr,
                                               QUuid headerID, const QUuid& connectionRequestID) {
    
    if (headerID.isNull()) {
        headerID = _sessionUUID;
    }
    
    QByteArray iceRequestByteArray = byteArrayWithPopulatedHeader(PacketTypeIceServerHeartbeat, headerID);
    QDataStream iceDataStream(&iceRequestByteArray, QIODevice::Append);
    
    iceDataStream << _publicSockAddr << _localSockAddr;
    
    if (!connectionRequestID.isNull()) {
        iceDataStream << connectionRequestID;
        
        qDebug() << "Sending packet to ICE server to request connection info for peer with ID"
            << uuidStringWithoutCurlyBraces(connectionRequestID);
    }
    
    writeUnverifiedDatagram(iceRequestByteArray, iceServerSockAddr);
}
