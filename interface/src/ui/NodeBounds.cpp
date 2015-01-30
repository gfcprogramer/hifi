//
//  NodeBounds.cpp
//  interface/src/ui
//
//  Created by Ryan Huffman on 05/14/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  This class draws a border around the different Entity nodes on the current domain,
//  and a semi-transparent cube around the currently mouse-overed node.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <DependencyManager.h>
#include <GeometryCache.h>

#include "Application.h"
#include "Util.h"

#include "NodeBounds.h"

NodeBounds::NodeBounds(QObject* parent) :
    QObject(parent),
    _showEntityNodes(false),
    _overlayText() {

}

void NodeBounds::draw() {
    if (!_showEntityNodes) {
        _overlayText[0] = '\0';
        return;
    }

    NodeToJurisdictionMap& entityServerJurisdictions = Application::getInstance()->getEntityServerJurisdictions();
    NodeToJurisdictionMap* serverJurisdictions;

    // Compute ray to find selected nodes later on.  We can't use the pre-computed ray in Application because it centers
    // itself after the cursor disappears.
    Application* application = Application::getInstance();
    PickRay pickRay = application->getCamera()->computePickRay(application->getTrueMouseX(),
                                                               application->getTrueMouseY());

    // Variables to keep track of the selected node and properties to draw the cube later if needed
    Node* selectedNode = NULL;
    float selectedDistance = FLT_MAX;
    bool selectedIsInside = true;
    glm::vec3 selectedCenter;
    float selectedScale = 0;

    auto nodeList = DependencyManager::get<NodeList>();
    nodeList->eachNode([&](const SharedNodePointer& node){
        NodeType_t nodeType = node->getType();
        
        if (nodeType == NodeType::EntityServer && _showEntityNodes) {
            serverJurisdictions = &entityServerJurisdictions;
        } else {
            return;
        }
        
        QUuid nodeUUID = node->getUUID();
        serverJurisdictions->lockForRead();
        if (serverJurisdictions->find(nodeUUID) != serverJurisdictions->end()) {
            const JurisdictionMap& map = (*serverJurisdictions)[nodeUUID];
            
            unsigned char* rootCode = map.getRootOctalCode();
            
            if (rootCode) {
                VoxelPositionSize rootDetails;
                voxelDetailsForCode(rootCode, rootDetails);
                serverJurisdictions->unlock();
                glm::vec3 location(rootDetails.x, rootDetails.y, rootDetails.z);
                location *= (float)TREE_SCALE;
                
                AACube serverBounds(location, rootDetails.s * TREE_SCALE);
                
                glm::vec3 center = serverBounds.getVertex(BOTTOM_RIGHT_NEAR)
                + ((serverBounds.getVertex(TOP_LEFT_FAR) - serverBounds.getVertex(BOTTOM_RIGHT_NEAR)) / 2.0f);
                
                const float ENTITY_NODE_SCALE = 0.99f;
                
                float scaleFactor = rootDetails.s * TREE_SCALE;
                
                // Scale by 0.92 - 1.00 depending on the scale of the node.  This allows smaller nodes to scale in
                // a bit and not overlap larger nodes.
                scaleFactor *= 0.92 + (rootDetails.s * 0.08);
                
                // Scale different node types slightly differently because it's common for them to overlap.
                if (nodeType == NodeType::EntityServer) {
                    scaleFactor *= ENTITY_NODE_SCALE;
                }
                
                float red, green, blue;
                getColorForNodeType(nodeType, red, green, blue);
                drawNodeBorder(center, scaleFactor, red, green, blue);
                
                float distance;
                BoxFace face;
                
                bool inside = serverBounds.contains(pickRay.origin);
                bool colliding = serverBounds.findRayIntersection(pickRay.origin, pickRay.direction, distance, face);

                // If the camera is inside a node it will be "selected" if you don't have your cursor over another node
                // that you aren't inside.
                if (colliding && (!selectedNode || (!inside && (distance < selectedDistance || selectedIsInside)))) {
                    selectedNode = node.data();
                    selectedDistance = distance;
                    selectedIsInside = inside;
                    selectedCenter = center;
                    selectedScale = scaleFactor;
                }
            } else {
                serverJurisdictions->unlock();
            }
        } else {
            serverJurisdictions->unlock();
        }
    });

    if (selectedNode) {
        glPushMatrix();

        glTranslatef(selectedCenter.x, selectedCenter.y, selectedCenter.z);
        glScalef(selectedScale, selectedScale, selectedScale);

        float red, green, blue;
        getColorForNodeType(selectedNode->getType(), red, green, blue);

        DependencyManager::get<GeometryCache>()->renderSolidCube(1.0f, glm::vec4(red, green, blue, 0.2f));

        glPopMatrix();

        HifiSockAddr addr = selectedNode->getPublicSocket();
        QString overlay = QString("%1:%2  %3ms")
            .arg(addr.getAddress().toString())
            .arg(addr.getPort())
            .arg(selectedNode->getPingMs())
            .left(MAX_OVERLAY_TEXT_LENGTH);

        // Ideally we'd just use a QString, but I ran into weird blinking issues using
        // constData() directly, as if the data was being overwritten.
        strcpy(_overlayText, overlay.toLocal8Bit().constData());
    } else {
        _overlayText[0] = '\0';
    }
}

void NodeBounds::drawNodeBorder(const glm::vec3& center, float scale, float red, float green, float blue) {
    glPushMatrix();
    glTranslatef(center.x, center.y, center.z);
    glScalef(scale, scale, scale);
    glLineWidth(2.5);
    DependencyManager::get<GeometryCache>()->renderWireCube(1.0f, glm::vec4(red, green, blue, 1.0f));
    glPopMatrix();
}

void NodeBounds::getColorForNodeType(NodeType_t nodeType, float& red, float& green, float& blue) {
    red = nodeType == 0.0;
    green = 0.0;
    blue = nodeType == NodeType::EntityServer ? 1.0 : 0.0;
}

void NodeBounds::drawOverlay() {
    if (strlen(_overlayText) > 0) {
        Application* application = Application::getInstance();

        const float TEXT_COLOR[] = { 0.90f, 0.90f, 0.90f };
        const float TEXT_SCALE = 0.1f;
        const int TEXT_HEIGHT = 10;
        const float ROTATION = 0.0f;
        const int FONT = 2;
        const int PADDING = 10;
        const int MOUSE_OFFSET = 10;
        const int BACKGROUND_BEVEL = 3;

        int mouseX = application->getTrueMouseX(),
            mouseY = application->getTrueMouseY(),
            textWidth = widthText(TEXT_SCALE, 0, _overlayText);
        DependencyManager::get<GeometryCache>()->renderBevelCornersRect(
                                mouseX + MOUSE_OFFSET, mouseY - TEXT_HEIGHT - PADDING,
                                textWidth + (2 * PADDING), TEXT_HEIGHT + (2 * PADDING), BACKGROUND_BEVEL,
                                glm::vec4(0.4f, 0.4f, 0.4f, 0.6f));
        drawText(mouseX + MOUSE_OFFSET + PADDING, mouseY, TEXT_SCALE, ROTATION, FONT, _overlayText, TEXT_COLOR);
    }
}
