//
//  EntityEditPacketSender.cpp
//  libraries/models/src
//
//  Created by Brad Hefta-Gaub on 8/12/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <assert.h>
#include <PerfStat.h>
#include <OctalCode.h>
#include <PacketHeaders.h>
#include "EntityEditPacketSender.h"
#include "EntityItem.h"


void EntityEditPacketSender::adjustEditPacketForClockSkew(PacketType type, 
                                        unsigned char* editBuffer, size_t length, int clockSkew) {
                                        
    if (type == PacketTypeEntityAddOrEdit) {
        EntityItem::adjustEditPacketForClockSkew(editBuffer, length, clockSkew);
    }
}

void EntityEditPacketSender::queueEditEntityMessage(PacketType type, EntityItemID modelID, 
                                                                const EntityItemProperties& properties) {
    if (!_shouldSend) {
        return; // bail early
    }

    // use MAX_PACKET_SIZE since it's static and guaranteed to be larger than _maxPacketSize
    unsigned char bufferOut[MAX_PACKET_SIZE];
    int sizeOut = 0;

    if (EntityItemProperties::encodeEntityEditPacket(type, modelID, properties, &bufferOut[0], _maxPacketSize, sizeOut)) {
        #ifdef WANT_DEBUG
            qDebug() << "calling queueOctreeEditMessage()...";
            qDebug() << "    id:" << modelID;
            qDebug() << "    properties:" << properties;
        #endif
        queueOctreeEditMessage(type, bufferOut, sizeOut);
    }
}

void EntityEditPacketSender::queueEraseEntityMessage(const EntityItemID& entityItemID) {
    if (!_shouldSend) {
        return; // bail early
    }
    // use MAX_PACKET_SIZE since it's static and guaranteed to be larger than _maxPacketSize
    unsigned char bufferOut[MAX_PACKET_SIZE];
    size_t sizeOut = 0;
    if (EntityItemProperties::encodeEraseEntityMessage(entityItemID, &bufferOut[0], _maxPacketSize, sizeOut)) {
        queueOctreeEditMessage(PacketTypeEntityErase, bufferOut, sizeOut);
    }
}
