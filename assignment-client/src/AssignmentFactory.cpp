//
//  AssignmentFactory.cpp
//  hifi
//
//  Created by Stephen Birarda on 9/17/13.
//  Copyright (c) 2013 HighFidelity, Inc. All rights reserved.
//

#include <PacketHeaders.h>

#include <ParticleServer.h>

#include <VoxelServer.h>

#include "Agent.h"
#include "AssignmentFactory.h"
#include "audio/AudioMixer.h"
#include "avatars/AvatarMixer.h"
#include "metavoxels/MetavoxelServer.h"

ThreadedAssignment* AssignmentFactory::unpackAssignment(const QByteArray& packet) {
    int headerBytes = numBytesForPacketHeader(packet);
    
    Assignment::Type assignmentType = Assignment::AllTypes;
    memcpy(&assignmentType, packet.data() + headerBytes, sizeof(Assignment::Type));
    
    switch (assignmentType) {
        case Assignment::AudioMixerType:
            return new AudioMixer(packet);
        case Assignment::AvatarMixerType:
            return new AvatarMixer(packet);
        case Assignment::AgentType:
            return new Agent(packet);
        case Assignment::VoxelServerType:
            return new VoxelServer(packet);
        case Assignment::ParticleServerType:
            return new ParticleServer(packet);
        case Assignment::MetavoxelServerType:
            return new MetavoxelServer(packet);
        default:
            return NULL;
    }
}
