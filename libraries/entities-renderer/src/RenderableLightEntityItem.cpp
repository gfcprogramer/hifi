//
//  RenderableLightEntityItem.cpp
//  interface/src
//
//  Created by Brad Hefta-Gaub on 8/6/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <glm/gtx/quaternion.hpp>

#include <gpu/GPUConfig.h>

#include <DeferredLightingEffect.h>
#include <PerfStat.h>

#include "RenderableLightEntityItem.h"

EntityItem* RenderableLightEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    return new RenderableLightEntityItem(entityID, properties);
}

void RenderableLightEntityItem::render(RenderArgs* args) {
    PerformanceTimer perfTimer("RenderableLightEntityItem::render");
    assert(getType() == EntityTypes::Light);
    glm::vec3 position = getPositionInMeters();
    glm::vec3 dimensions = getDimensions() * (float)TREE_SCALE;
    glm::quat rotation = getRotation();
    float largestDiameter = glm::max(dimensions.x, dimensions.y, dimensions.z);

    const float MAX_COLOR = 255.0f;
    float diffuseR = getDiffuseColor()[RED_INDEX] / MAX_COLOR;
    float diffuseG = getDiffuseColor()[GREEN_INDEX] / MAX_COLOR;
    float diffuseB = getDiffuseColor()[BLUE_INDEX] / MAX_COLOR;

    float ambientR = getAmbientColor()[RED_INDEX] / MAX_COLOR;
    float ambientG = getAmbientColor()[GREEN_INDEX] / MAX_COLOR;
    float ambientB = getAmbientColor()[BLUE_INDEX] / MAX_COLOR;

    float specularR = getSpecularColor()[RED_INDEX] / MAX_COLOR;
    float specularG = getSpecularColor()[GREEN_INDEX] / MAX_COLOR;
    float specularB = getSpecularColor()[BLUE_INDEX] / MAX_COLOR;

    glm::vec3 ambient = glm::vec3(ambientR, ambientG, ambientB);
    glm::vec3 diffuse = glm::vec3(diffuseR, diffuseG, diffuseB);
    glm::vec3 specular = glm::vec3(specularR, specularG, specularB);
    glm::vec3 direction = IDENTITY_FRONT * rotation;
    float constantAttenuation = getConstantAttenuation();
    float linearAttenuation = getLinearAttenuation();
    float quadraticAttenuation = getQuadraticAttenuation();
    float exponent = getExponent();
    float cutoff = glm::radians(getCutoff());

    if (_isSpotlight) {
        DependencyManager::get<DeferredLightingEffect>()->addSpotLight(position, largestDiameter / 2.0f, 
            ambient, diffuse, specular, constantAttenuation, linearAttenuation, quadraticAttenuation,
            direction, exponent, cutoff);
    } else {
        DependencyManager::get<DeferredLightingEffect>()->addPointLight(position, largestDiameter / 2.0f, 
            ambient, diffuse, specular, constantAttenuation, linearAttenuation, quadraticAttenuation);
    }

#ifdef WANT_DEBUG
    glm::vec4 color(diffuseR, diffuseG, diffuseB, 1.0f);
    glPushMatrix();
        glTranslatef(position.x, position.y, position.z);
        glm::vec3 axis = glm::axis(rotation);
        glRotatef(glm::degrees(glm::angle(rotation)), axis.x, axis.y, axis.z);
        glPushMatrix();
            glm::vec3 positionToCenter = center - position;
            glTranslatef(positionToCenter.x, positionToCenter.y, positionToCenter.z);

            glScalef(dimensions.x, dimensions.y, dimensions.z);
            DependencyManager::get<DeferredLightingEffect>()->renderWireSphere(0.5f, 15, 15, color);
        glPopMatrix();
    glPopMatrix();
#endif
};

bool RenderableLightEntityItem::findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                         bool& keepSearching, OctreeElement*& element, float& distance, BoxFace& face, 
                         void** intersectedObject, bool precisionPicking) const {

    // TODO: consider if this is really what we want to do. We've made it so that "lights are pickable" is a global state
    // this is probably reasonable since there's typically only one tree you'd be picking on at a time. Technically we could 
    // be on the clipboard and someone might be trying to use the ray intersection API there. Anyway... if you ever try to 
    // do ray intersection testing off of trees other than the main tree of the main entity renderer, then we'll need to 
    // fix this mechanism.
    return _lightsArePickable;
}
