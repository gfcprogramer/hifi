//
//  Endpoint.h
//  libraries/metavoxels/src
//
//  Created by Andrzej Kapolka on 6/26/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Endpoint_h
#define hifi_Endpoint_h

#include <NodeList.h>

#include "DatagramSequencer.h"
#include "MetavoxelData.h"
#include "Spanner.h"

class PacketRecord;

/// Base class for communication endpoints: clients and server sessions.
class Endpoint : public NodeData {
    Q_OBJECT

public:

    /// The index of the input/output channel used to transmit reliable deltas.
    static const int RELIABLE_DELTA_CHANNEL_INDEX = 1;
    
    Endpoint(const SharedNodePointer& node, PacketRecord* baselineSendRecord = NULL,
        PacketRecord* baselineReceiveRecord = NULL);
    virtual ~Endpoint();
    
    DatagramSequencer& getSequencer() { return _sequencer; }
    
    virtual void update();
    
    virtual int parseData(const QByteArray& packet);

protected slots:

    virtual void sendDatagram(const QByteArray& data);
    virtual void readMessage(Bitstream& in);
    virtual void handleMessage(const QVariant& message, Bitstream& in); 
    
    void recordSend();
    virtual void recordReceive();
    
    virtual void clearSendRecordsBefore(int index);
    virtual void clearReceiveRecordsBefore(int index);

protected:

    virtual void writeUpdateMessage(Bitstream& out);
    
    virtual PacketRecord* maybeCreateSendRecord() const;
    virtual PacketRecord* maybeCreateReceiveRecord() const;
    
    PacketRecord* getLastAcknowledgedSendRecord() const { return _sendRecords.first(); }
    PacketRecord* getLastAcknowledgedReceiveRecord() const { return _receiveRecords.first(); }
    
    SharedNodePointer _node;
    DatagramSequencer _sequencer;
    
    QList<PacketRecord*> _sendRecords;
    QList<PacketRecord*> _receiveRecords;
};

/// Base class for packet records.
class PacketRecord {
public:

    PacketRecord(int packetNumber = 0, const MetavoxelLOD& lod = MetavoxelLOD(), const MetavoxelData& data = MetavoxelData());
    virtual ~PacketRecord();
    
    int getPacketNumber() const { return _packetNumber; }
    const MetavoxelLOD& getLOD() const { return _lod; }
    const MetavoxelData& getData() const { return _data; }
    
private:
    
    int _packetNumber;
    MetavoxelLOD _lod;
    MetavoxelData _data;
};

#endif // hifi_Endpoint_h
