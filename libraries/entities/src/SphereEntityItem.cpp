//
//  SphereEntityItem.cpp
//  libraries/entities/src
//
//  Created by Brad Hefta-Gaub on 12/4/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//


#include <glm/gtx/transform.hpp>

#include <QDebug>

#include <ByteCountCoding.h>
#include <GeometryUtil.h>

#include "EntityTree.h"
#include "EntityTreeElement.h"
#include "SphereEntityItem.h"


EntityItem* SphereEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    return new SphereEntityItem(entityID, properties);
}

// our non-pure virtual subclass for now...
SphereEntityItem::SphereEntityItem(const EntityItemID& entityItemID, const EntityItemProperties& properties) :
        EntityItem(entityItemID, properties) 
{ 
    _type = EntityTypes::Sphere;
    setProperties(properties);
    // NOTE: _volumeMultiplier is used to compute volume:
    // volume = _volumeMultiplier * _dimensions.x * _dimensions.y * _dimensions.z
    // The formula below looks funny because _dimension.xyz = diameter rather than radius.
    _volumeMultiplier *= PI / 6.0f;
}

EntityItemProperties SphereEntityItem::getProperties() const {
    EntityItemProperties properties = EntityItem::getProperties(); // get the properties from our base class
    properties.setColor(getXColor());
    return properties;
}

bool SphereEntityItem::setProperties(const EntityItemProperties& properties) {
    bool somethingChanged = EntityItem::setProperties(properties); // set the properties in our base class

    SET_ENTITY_PROPERTY_FROM_PROPERTIES(color, setColor);

    if (somethingChanged) {
        bool wantDebug = false;
        if (wantDebug) {
            uint64_t now = usecTimestampNow();
            int elapsed = now - getLastEdited();
            qDebug() << "SphereEntityItem::setProperties() AFTER update... edited AGO=" << elapsed <<
                    "now=" << now << " getLastEdited()=" << getLastEdited();
        }
        setLastEdited(properties.getLastEdited());
    }
    return somethingChanged;
}

int SphereEntityItem::readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead, 
                                                ReadBitstreamToTreeParams& args,
                                                EntityPropertyFlags& propertyFlags, bool overwriteLocalData) {

    int bytesRead = 0;
    const unsigned char* dataAt = data;

    READ_ENTITY_PROPERTY_COLOR(PROP_COLOR, _color);

    return bytesRead;
}


// TODO: eventually only include properties changed since the params.lastViewFrustumSent time
EntityPropertyFlags SphereEntityItem::getEntityProperties(EncodeBitstreamParams& params) const {
    EntityPropertyFlags requestedProperties = EntityItem::getEntityProperties(params);
    requestedProperties += PROP_COLOR;
    return requestedProperties;
}

void SphereEntityItem::appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params, 
                                    EntityTreeElementExtraEncodeData* modelTreeElementExtraEncodeData,
                                    EntityPropertyFlags& requestedProperties,
                                    EntityPropertyFlags& propertyFlags,
                                    EntityPropertyFlags& propertiesDidntFit,
                                    int& propertyCount, 
                                    OctreeElement::AppendState& appendState) const { 

    bool successPropertyFits = true;
    APPEND_ENTITY_PROPERTY(PROP_COLOR, appendColor, getColor());
}

void SphereEntityItem::recalculateCollisionShape() {
    _sphereShape.setTranslation(getCenterInMeters());
    glm::vec3 dimensionsInMeters = getDimensionsInMeters();
    float largestDiameter = glm::max(dimensionsInMeters.x, dimensionsInMeters.y, dimensionsInMeters.z);
    _sphereShape.setRadius(largestDiameter / 2.0f);
}

void SphereEntityItem::computeShapeInfo(ShapeInfo& info) const {
    glm::vec3 halfExtents = 0.5f * getDimensionsInMeters();
    // TODO: support ellipsoid shapes
    info.setSphere(halfExtents.x);
}

bool SphereEntityItem::findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                     bool& keepSearching, OctreeElement*& element, float& distance, BoxFace& face, 
                     void** intersectedObject, bool precisionPicking) const {
    // determine the ray in the frame of the entity transformed from a unit sphere
    glm::mat4 translation = glm::translate(getPosition());
    glm::mat4 rotation = glm::mat4_cast(getRotation());
    glm::mat4 scale = glm::scale(getDimensions());
    glm::mat4 registration = glm::translate(glm::vec3(0.5f, 0.5f, 0.5f) - getRegistrationPoint());
    glm::mat4 entityToWorldMatrix = translation * rotation * scale * registration;
    glm::mat4 worldToEntityMatrix = glm::inverse(entityToWorldMatrix);
    glm::vec3 entityFrameOrigin = glm::vec3(worldToEntityMatrix * glm::vec4(origin, 1.0f));
    glm::vec3 entityFrameDirection = glm::normalize(glm::vec3(worldToEntityMatrix * glm::vec4(direction, 0.0f)));

    float localDistance;
    // NOTE: unit sphere has center of 0,0,0 and radius of 0.5
    if (findRaySphereIntersection(entityFrameOrigin, entityFrameDirection, glm::vec3(0.0f), 0.5f, localDistance)) {
        // determine where on the unit sphere the hit point occured
        glm::vec3 entityFrameHitAt = entityFrameOrigin + (entityFrameDirection * localDistance);
        // then translate back to work coordinates
        glm::vec3 hitAt = glm::vec3(entityToWorldMatrix * glm::vec4(entityFrameHitAt, 1.0f));
        distance = glm::distance(origin,hitAt);
        return true;
    }
    return false;                
}


void SphereEntityItem::debugDump() const {
    quint64 now = usecTimestampNow();
    qDebug() << "SHPERE EntityItem id:" << getEntityItemID() << "---------------------------------------------";
    qDebug() << "               color:" << _color[0] << "," << _color[1] << "," << _color[2];
    qDebug() << "            position:" << debugTreeVector(_position);
    qDebug() << "          dimensions:" << debugTreeVector(_dimensions);
    qDebug() << "       getLastEdited:" << debugTime(getLastEdited(), now);
}

