//
//  OctreeRenderer.cpp
//  libraries/octree/src
//
//  Created by Brad Hefta-Gaub on 12/6/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <glm/glm.hpp>
#include <stdint.h>

#include <SharedUtil.h>
#include <PerfStat.h>
#include <RenderArgs.h>
#include "OctreeRenderer.h"

OctreeRenderer::OctreeRenderer() :
    _tree(NULL),
    _managedTree(false),
    _viewFrustum(NULL)
{
}

void OctreeRenderer::init() {
    if (!_tree) {
        _tree = createTree();
        _managedTree = true;
    }
}

OctreeRenderer::~OctreeRenderer() {
    if (_tree && _managedTree) {
        delete _tree;
    }
}

void OctreeRenderer::setTree(Octree* newTree) { 
    if (_tree && _managedTree) {
        delete _tree;
        _managedTree = false;
    }
    _tree = newTree; 
}

void OctreeRenderer::processDatagram(const QByteArray& dataByteArray, const SharedNodePointer& sourceNode) {
    bool extraDebugging = false;
    
    if (extraDebugging) {
        qDebug() << "OctreeRenderer::processDatagram()";
    }

    if (!_tree) {
        qDebug() << "OctreeRenderer::processDatagram() called before init, calling init()...";
        this->init();
    }

    bool showTimingDetails = false; // Menu::getInstance()->isOptionChecked(MenuOption::PipelineWarnings);
    PerformanceWarning warn(showTimingDetails, "OctreeRenderer::processDatagram()",showTimingDetails);
    
    unsigned int packetLength = dataByteArray.size();
    PacketType command = packetTypeForPacket(dataByteArray);
    unsigned int numBytesPacketHeader = numBytesForPacketHeader(dataByteArray);
    QUuid sourceUUID = uuidFromPacketHeader(dataByteArray);
    PacketType expectedType = getExpectedPacketType();
    PacketVersion expectedVersion = _tree->expectedVersion(); // TODO: would be better to read this from the packet!
    
    if(command == expectedType) {
        PerformanceWarning warn(showTimingDetails, "OctreeRenderer::processDatagram expected PacketType", showTimingDetails);
        // if we are getting inbound packets, then our tree is also viewing, and we should remember that fact.
        _tree->setIsViewing(true);

        const unsigned char* dataAt = reinterpret_cast<const unsigned char*>(dataByteArray.data()) + numBytesPacketHeader;

        OCTREE_PACKET_FLAGS flags = (*(OCTREE_PACKET_FLAGS*)(dataAt));
        dataAt += sizeof(OCTREE_PACKET_FLAGS);
        OCTREE_PACKET_SEQUENCE sequence = (*(OCTREE_PACKET_SEQUENCE*)dataAt);
        dataAt += sizeof(OCTREE_PACKET_SEQUENCE);

        OCTREE_PACKET_SENT_TIME sentAt = (*(OCTREE_PACKET_SENT_TIME*)dataAt);
        dataAt += sizeof(OCTREE_PACKET_SENT_TIME);

        bool packetIsColored = oneAtBit(flags, PACKET_IS_COLOR_BIT);
        bool packetIsCompressed = oneAtBit(flags, PACKET_IS_COMPRESSED_BIT);
        
        OCTREE_PACKET_SENT_TIME arrivedAt = usecTimestampNow();
        int clockSkew = sourceNode ? sourceNode->getClockSkewUsec() : 0;
        int flightTime = arrivedAt - sentAt + clockSkew;

        OCTREE_PACKET_INTERNAL_SECTION_SIZE sectionLength = 0;
        unsigned int dataBytes = packetLength - (numBytesPacketHeader + OCTREE_PACKET_EXTRA_HEADERS_SIZE);

        if (extraDebugging) {
            qDebug("OctreeRenderer::processDatagram() ... Got Packet Section"
                   " color:%s compressed:%s sequence: %u flight:%d usec size:%u data:%u",
                   debug::valueOf(packetIsColored), debug::valueOf(packetIsCompressed),
                   sequence, flightTime, packetLength, dataBytes);
        }
        
        int subsection = 1;
        while (dataBytes > 0) {
            if (packetIsCompressed) {
                if (dataBytes > sizeof(OCTREE_PACKET_INTERNAL_SECTION_SIZE)) {
                    sectionLength = (*(OCTREE_PACKET_INTERNAL_SECTION_SIZE*)dataAt);
                    dataAt += sizeof(OCTREE_PACKET_INTERNAL_SECTION_SIZE);
                    dataBytes -= sizeof(OCTREE_PACKET_INTERNAL_SECTION_SIZE);
                } else {
                    sectionLength = 0;
                    dataBytes = 0; // stop looping something is wrong
                }
            } else {
                sectionLength = dataBytes;
            }
            
            if (sectionLength) {
                // ask the VoxelTree to read the bitstream into the tree
                ReadBitstreamToTreeParams args(packetIsColored ? WANT_COLOR : NO_COLOR, WANT_EXISTS_BITS, NULL, 
                                                sourceUUID, sourceNode, false, expectedVersion);
                _tree->lockForWrite();
                OctreePacketData packetData(packetIsCompressed);
                packetData.loadFinalizedContent(dataAt, sectionLength);
                if (extraDebugging) {
                    qDebug("OctreeRenderer::processDatagram() ... Got Packet Section"
                           " color:%s compressed:%s sequence: %u flight:%d usec size:%u data:%u"
                           " subsection:%d sectionLength:%d uncompressed:%d",
                           debug::valueOf(packetIsColored), debug::valueOf(packetIsCompressed),
                           sequence, flightTime, packetLength, dataBytes, subsection, sectionLength,
                           packetData.getUncompressedSize());
                }
                if (extraDebugging) {
                    qDebug() << "OctreeRenderer::processDatagram() ******* START _tree->readBitstreamToTree()...";
                }
                _tree->readBitstreamToTree(packetData.getUncompressedData(), packetData.getUncompressedSize(), args);
                if (extraDebugging) {
                    qDebug() << "OctreeRenderer::processDatagram() ******* END _tree->readBitstreamToTree()...";
                }
                _tree->unlock();
            
                dataBytes -= sectionLength;
                dataAt += sectionLength;
            }
        }
        subsection++;
    }
}

bool OctreeRenderer::renderOperation(OctreeElement* element, void* extraData) {
    RenderArgs* args = static_cast<RenderArgs*>(extraData);
    if (element->isInView(*args->_viewFrustum)) {
        if (element->hasContent()) {
            if (element->calculateShouldRender(args->_viewFrustum, args->_sizeScale, args->_boundaryLevelAdjust)) {
                args->_renderer->renderElement(element, args);
            } else {
                return false; // if we shouldn't render, then we also should stop recursing.
            }
        }
        return true; // continue recursing
    }
    // if not in view stop recursing
    return false;
}

void OctreeRenderer::render(RenderArgs::RenderMode renderMode, RenderArgs::RenderSide renderSide) {
    RenderArgs args = { this, _viewFrustum, getSizeScale(), getBoundaryLevelAdjust(), renderMode, renderSide, 
                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (_tree) {
        _tree->lockForRead();
        _tree->recurseTreeWithOperation(renderOperation, &args);
        _tree->unlock();
    }
    _meshesConsidered = args._meshesConsidered;
    _meshesRendered = args._meshesRendered;
    _meshesOutOfView = args._meshesOutOfView;
    _meshesTooSmall = args._meshesTooSmall;

    _elementsTouched = args._elementsTouched;
    _itemsRendered = args._itemsRendered;
    _itemsOutOfView = args._itemsOutOfView;
    _itemsTooSmall = args._itemsTooSmall;

    _materialSwitches = args._materialSwitches;
    _trianglesRendered = args._trianglesRendered;
    _quadsRendered = args._quadsRendered;

    _translucentMeshPartsRendered = args._translucentMeshPartsRendered;
    _opaqueMeshPartsRendered = args._opaqueMeshPartsRendered;

}

void OctreeRenderer::clear() { 
    if (_tree) {
        _tree->lockForWrite();
        _tree->eraseAllOctreeElements(); 
        _tree->unlock();
    }
}

