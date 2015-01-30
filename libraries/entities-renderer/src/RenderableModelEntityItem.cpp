//
//  RenderableModelEntityItem.cpp
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

#include <QJsonDocument>

#include <DeferredLightingEffect.h>
#include <Model.h>
#include <PerfStat.h>

#include "EntityTreeRenderer.h"
#include "RenderableModelEntityItem.h"

EntityItem* RenderableModelEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    return new RenderableModelEntityItem(entityID, properties);
}

RenderableModelEntityItem::~RenderableModelEntityItem() {
    assert(_myRenderer || !_model); // if we have a model, we need to know our renderer
    if (_myRenderer && _model) {
        _myRenderer->releaseModel(_model);
        _model = NULL;
    }
}

bool RenderableModelEntityItem::setProperties(const EntityItemProperties& properties) {
    QString oldModelURL = getModelURL();
    bool somethingChanged = ModelEntityItem::setProperties(properties);
    if (somethingChanged && oldModelURL != getModelURL()) {
        _needsModelReload = true;
    }
    return somethingChanged;
}

int RenderableModelEntityItem::readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead, 
                                                ReadBitstreamToTreeParams& args,
                                                EntityPropertyFlags& propertyFlags, bool overwriteLocalData) {
    QString oldModelURL = getModelURL();
    int bytesRead = ModelEntityItem::readEntitySubclassDataFromBuffer(data, bytesLeftToRead, 
                                                                        args, propertyFlags, overwriteLocalData);
    if (oldModelURL != getModelURL()) {
        _needsModelReload = true;
    }
    return bytesRead;
}

void RenderableModelEntityItem::remapTextures() {
    if (!_model) {
        return; // nothing to do if we don't have a model
    }
    
    if (!_model->isLoadedWithTextures()) {
        return; // nothing to do if the model has not yet loaded it's default textures
    }
    
    if (!_originalTexturesRead && _model->isLoadedWithTextures()) {
        const QSharedPointer<NetworkGeometry>& networkGeometry = _model->getGeometry();
        if (networkGeometry) {
            _originalTextures = networkGeometry->getTextureNames();
            _originalTexturesRead = true;
        }
    }
    
    if (_currentTextures == _textures) {
        return; // nothing to do if our recently mapped textures match our desired textures
    }
    
    // since we're changing here, we need to run through our current texture map
    // and any textures in the recently mapped texture, that is not in our desired
    // textures, we need to "unset"
    QJsonDocument currentTexturesAsJson = QJsonDocument::fromJson(_currentTextures.toUtf8());
    QJsonObject currentTexturesAsJsonObject = currentTexturesAsJson.object();
    QVariantMap currentTextureMap = currentTexturesAsJsonObject.toVariantMap();

    QJsonDocument texturesAsJson = QJsonDocument::fromJson(_textures.toUtf8());
    QJsonObject texturesAsJsonObject = texturesAsJson.object();
    QVariantMap textureMap = texturesAsJsonObject.toVariantMap();

    foreach(const QString& key, currentTextureMap.keys()) {
        // if the desired texture map (what we're setting the textures to) doesn't
        // contain this texture, then remove it by setting the URL to null
        if (!textureMap.contains(key)) {
            QUrl noURL;
            qDebug() << "Removing texture named" << key << "by replacing it with no URL";
            _model->setTextureWithNameToURL(key, noURL);
        }
    }

    // here's where we remap any textures if needed...
    foreach(const QString& key, textureMap.keys()) {
        QUrl newTextureURL = textureMap[key].toUrl();
        qDebug() << "Updating texture named" << key << "to texture at URL" << newTextureURL;
        _model->setTextureWithNameToURL(key, newTextureURL);
    }
    
    _currentTextures = _textures;
}


void RenderableModelEntityItem::render(RenderArgs* args) {
    PerformanceTimer perfTimer("RMEIrender");
    assert(getType() == EntityTypes::Model);
    
    bool drawAsModel = hasModel();

    glm::vec3 position = getPosition() * (float)TREE_SCALE;
    float size = getSize() * (float)TREE_SCALE;
    glm::vec3 dimensions = getDimensions() * (float)TREE_SCALE;
    
    if (drawAsModel) {
        remapTextures();
        glPushMatrix();
        {
            float alpha = getLocalRenderAlpha();

            if (!_model || _needsModelReload) {
                // TODO: this getModel() appears to be about 3% of model render time. We should optimize
                PerformanceTimer perfTimer("getModel");
                EntityTreeRenderer* renderer = static_cast<EntityTreeRenderer*>(args->_renderer);
                getModel(renderer);
            }
            
            if (_model) {
                // handle animations..
                if (hasAnimation()) {
                    if (!jointsMapped()) {
                        QStringList modelJointNames = _model->getJointNames();
                        mapJoints(modelJointNames);
                    }

                    if (jointsMapped()) {
                        QVector<glm::quat> frameData = getAnimationFrame();
                        for (int i = 0; i < frameData.size(); i++) {
                            _model->setJointState(i, true, frameData[i]);
                        }
                    }
                }

                glm::quat rotation = getRotation();
                if (needsToCallUpdate() && _model->isActive()) {
                    _model->setScaleToFit(true, dimensions);
                    _model->setSnapModelToRegistrationPoint(true, getRegistrationPoint());
                    _model->setRotation(rotation);
                    _model->setTranslation(position);
                    
                    // make sure to simulate so everything gets set up correctly for rendering
                    {
                        PerformanceTimer perfTimer("_model->simulate");
                        _model->simulate(0.0f);
                    }
                    _needsInitialSimulation = false;
                }

                if (_model->isActive()) {
                    // TODO: this is the majority of model render time. And rendering of a cube model vs the basic Box render
                    // is significantly more expensive. Is there a way to call this that doesn't cost us as much? 
                    PerformanceTimer perfTimer("model->render");
                    // filter out if not needed to render
                    if (args && (args->_renderMode == RenderArgs::SHADOW_RENDER_MODE)) {
                        if (isMoving() || isAnimatingSomething()) {
                            _model->renderInScene(alpha, args);
                        }
                    } else {
                        _model->renderInScene(alpha, args);
                    }
                } else {
                    // if we couldn't get a model, then just draw a cube
                    glm::vec4 color(getColor()[RED_INDEX]/255, getColor()[GREEN_INDEX]/255, getColor()[BLUE_INDEX]/255, 1.0f);
                    glPushMatrix();
                        glTranslatef(position.x, position.y, position.z);
                        DependencyManager::get<DeferredLightingEffect>()->renderWireCube(size, color);
                    glPopMatrix();
                }
            } else {
                // if we couldn't get a model, then just draw a cube
                glm::vec4 color(getColor()[RED_INDEX]/255, getColor()[GREEN_INDEX]/255, getColor()[BLUE_INDEX]/255, 1.0f);
                glPushMatrix();
                    glTranslatef(position.x, position.y, position.z);
                    DependencyManager::get<DeferredLightingEffect>()->renderWireCube(size, color);
                glPopMatrix();
            }
        }
        glPopMatrix();
    } else {
        glm::vec4 color(getColor()[RED_INDEX]/255, getColor()[GREEN_INDEX]/255, getColor()[BLUE_INDEX]/255, 1.0f);
        glPushMatrix();
        glTranslatef(position.x, position.y, position.z);
        DependencyManager::get<DeferredLightingEffect>()->renderWireCube(size, color);
        glPopMatrix();
    }
}

Model* RenderableModelEntityItem::getModel(EntityTreeRenderer* renderer) {
    Model* result = NULL;

    // make sure our renderer is setup
    if (!_myRenderer) {
        _myRenderer = renderer;
    }
    assert(_myRenderer == renderer); // you should only ever render on one renderer
    
    if (QThread::currentThread() != _myRenderer->thread()) {
        return _model;
    }
    
    _needsModelReload = false; // this is the reload

    // if we have a URL, then we will want to end up returning a model...
    if (!getModelURL().isEmpty()) {
    
        // if we have a previously allocated model, but it's URL doesn't match
        // then we need to let our renderer update our model for us.
        if (_model && QUrl(getModelURL()) != _model->getURL()) {
            result = _model = _myRenderer->updateModel(_model, getModelURL());
            _needsInitialSimulation = true;
        } else if (!_model) { // if we don't yet have a model, then we want our renderer to allocate one
            result = _model = _myRenderer->allocateModel(getModelURL());
            _needsInitialSimulation = true;
        } else { // we already have the model we want...
            result = _model;
        }
    } else { // if our desired URL is empty, we may need to delete our existing model
        if (_model) {
            _myRenderer->releaseModel(_model);
            result = _model = NULL;
            _needsInitialSimulation = true;
        }
    }
    
    return result;
}

bool RenderableModelEntityItem::needsToCallUpdate() const {
    return _needsInitialSimulation || ModelEntityItem::needsToCallUpdate();
}

EntityItemProperties RenderableModelEntityItem::getProperties() const {
    EntityItemProperties properties = ModelEntityItem::getProperties(); // get the properties from our base class
    if (_originalTexturesRead) {
        properties.setTextureNames(_originalTextures);
    }
    return properties;
}

bool RenderableModelEntityItem::findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                         bool& keepSearching, OctreeElement*& element, float& distance, BoxFace& face, 
                         void** intersectedObject, bool precisionPicking) const {
    if (!_model) {
        return true;
    }
    
    glm::vec3 originInMeters = origin * (float)TREE_SCALE;
    QString extraInfo;
    float localDistance;
    
    //qDebug() << "RenderableModelEntityItem::findDetailedRayIntersection() precisionPicking:" << precisionPicking;
    
    bool intersectsModel = _model->findRayIntersectionAgainstSubMeshes(originInMeters, direction, 
                                            localDistance, face, extraInfo, precisionPicking);
    
    if (intersectsModel) {
        // NOTE: findRayIntersectionAgainstSubMeshes() does work in meters, but we're expected to return
        // results in tree scale.
        distance = localDistance / (float)TREE_SCALE;
    }

    return intersectsModel; // we only got here if we intersected our non-aabox                         
}


