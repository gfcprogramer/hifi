//
//  Avatar.cpp
//  interface/src/avatar
//
//  Created by Philip Rosedale on 9/11/12.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <vector>

#include <gpu/GPUConfig.h>

#include <QDesktopWidget>
#include <QWindow>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/vector_query.hpp>

#include <DeferredLightingEffect.h>
#include <GeometryUtil.h>
#include <GlowEffect.h>
#include <LODManager.h>
#include <NodeList.h>
#include <PacketHeaders.h>
#include <PathUtils.h>
#include <PerfStat.h>
#include <SharedUtil.h>
#include <TextRenderer.h>
#include <TextureCache.h>

#include "Application.h"
#include "Avatar.h"
#include "Hand.h"
#include "Head.h"
#include "Menu.h"
#include "ModelReferential.h"
#include "Physics.h"
#include "Recorder.h"
#include "Util.h"
#include "world.h"
#include "devices/OculusManager.h"

using namespace std;

const glm::vec3 DEFAULT_UP_DIRECTION(0.0f, 1.0f, 0.0f);
const int   NUM_BODY_CONE_SIDES = 9;
const float CHAT_MESSAGE_SCALE = 0.0015f;
const float CHAT_MESSAGE_HEIGHT = 0.1f;
const float DISPLAYNAME_FADE_TIME = 0.5f;
const float DISPLAYNAME_FADE_FACTOR = pow(0.01f, 1.0f / DISPLAYNAME_FADE_TIME);
const float DISPLAYNAME_ALPHA = 0.95f;
const float DISPLAYNAME_BACKGROUND_ALPHA = 0.4f;

Avatar::Avatar() :
    AvatarData(),
    _skeletonModel(this),
    _skeletonOffset(0.0f),
    _bodyYawDelta(0.0f),
    _velocity(0.0f),
    _positionDeltaAccumulator(0.0f),
    _lastVelocity(0.0f),
    _acceleration(0.0f),
    _angularVelocity(0.0f),
    _lastAngularVelocity(0.0f),
    _angularAcceleration(0.0f),
    _lastOrientation(),
    _leanScale(0.5f),
    _scale(1.0f),
    _worldUpDirection(DEFAULT_UP_DIRECTION),
    _moving(false),
    _collisionGroups(0),
    _initialized(false),
    _shouldRenderBillboard(true)
{
    // we may have been created in the network thread, but we live in the main thread
    moveToThread(Application::getInstance()->thread());
    
    // give the pointer to our head to inherited _headData variable from AvatarData
    _headData = static_cast<HeadData*>(new Head(this));
    _handData = static_cast<HandData*>(new Hand(this));
}

Avatar::~Avatar() {
}

const float BILLBOARD_LOD_DISTANCE = 40.0f;

void Avatar::init() {
    getHead()->init();
    _skeletonModel.init();
    _initialized = true;
    _shouldRenderBillboard = (getLODDistance() >= BILLBOARD_LOD_DISTANCE);
}

glm::vec3 Avatar::getChestPosition() const {
    // for now, let's just assume that the "chest" is halfway between the root and the neck
    glm::vec3 neckPosition;
    return _skeletonModel.getNeckPosition(neckPosition) ? (_position + neckPosition) * 0.5f : _position;
}

glm::vec3 Avatar::getNeckPosition() const {
    glm::vec3 neckPosition;
    return _skeletonModel.getNeckPosition(neckPosition) ? neckPosition : _position;
}


glm::quat Avatar::getWorldAlignedOrientation () const {
    return computeRotationFromBodyToWorldUp() * getOrientation();
}

float Avatar::getLODDistance() const {
    return DependencyManager::get<LODManager>()->getAvatarLODDistanceMultiplier() *
            glm::distance(qApp->getCamera()->getPosition(), _position) / _scale;
}

void Avatar::simulate(float deltaTime) {
    PerformanceTimer perfTimer("simulate");
    
    // update the avatar's position according to its referential
    if (_referential) {
        if (_referential->hasExtraData()) {
            EntityTree* tree = Application::getInstance()->getEntities()->getTree();
            switch (_referential->type()) {
                case Referential::MODEL:
                    _referential = new ModelReferential(_referential,
                                                        tree,
                                                        this);
                    break;
                case Referential::JOINT:
                    _referential = new JointReferential(_referential,
                                                        tree,
                                                        this);
                    break;
                default:
                    qDebug() << "[WARNING] Avatar::simulate(): Unknown referential type.";
                    break;
            }
        }
        
        _referential->update();
    }
    
    if (_scale != _targetScale) {
        setScale(_targetScale);
    }

    // update the billboard render flag
    const float BILLBOARD_HYSTERESIS_PROPORTION = 0.1f;
    if (_shouldRenderBillboard) {
        if (getLODDistance() < BILLBOARD_LOD_DISTANCE * (1.0f - BILLBOARD_HYSTERESIS_PROPORTION)) {
            _shouldRenderBillboard = false;
        }
    } else if (getLODDistance() > BILLBOARD_LOD_DISTANCE * (1.0f + BILLBOARD_HYSTERESIS_PROPORTION)) {
        _shouldRenderBillboard = true;
    }

    // simple frustum check
    float boundingRadius = getBillboardSize();
    bool inViewFrustum = Application::getInstance()->getViewFrustum()->sphereInFrustum(_position, boundingRadius) !=
        ViewFrustum::OUTSIDE;

    {
        PerformanceTimer perfTimer("hand");
        getHand()->simulate(deltaTime, false);
    }
    _skeletonModel.setLODDistance(getLODDistance());
    
    if (!_shouldRenderBillboard && inViewFrustum) {
        {
            PerformanceTimer perfTimer("skeleton");
            if (_hasNewJointRotations) {
                for (int i = 0; i < _jointData.size(); i++) {
                    const JointData& data = _jointData.at(i);
                    _skeletonModel.setJointState(i, data.valid, data.rotation);
                }
            }
            _skeletonModel.simulate(deltaTime, _hasNewJointRotations);
            simulateAttachments(deltaTime);
            _hasNewJointRotations = false;
        }
        {
            PerformanceTimer perfTimer("head");
            glm::vec3 headPosition = _position;
            _skeletonModel.getHeadPosition(headPosition);
            Head* head = getHead();
            head->setPosition(headPosition);
            head->setScale(_scale);
            head->simulate(deltaTime, false, _shouldRenderBillboard);
        }
    }

    // update animation for display name fade in/out
    if ( _displayNameTargetAlpha != _displayNameAlpha) {
        // the alpha function is 
        // Fade out => alpha(t) = factor ^ t => alpha(t+dt) = alpha(t) * factor^(dt)
        // Fade in  => alpha(t) = 1 - factor^t => alpha(t+dt) = 1-(1-alpha(t))*coef^(dt)
        // factor^(dt) = coef
        float coef = pow(DISPLAYNAME_FADE_FACTOR, deltaTime);
        if (_displayNameTargetAlpha < _displayNameAlpha) {
            // Fading out
            _displayNameAlpha *= coef;
        } else {
            // Fading in
            _displayNameAlpha = 1 - (1 - _displayNameAlpha) * coef;
        }
        _displayNameAlpha = abs(_displayNameAlpha - _displayNameTargetAlpha) < 0.01f ? _displayNameTargetAlpha : _displayNameAlpha;
    }

    // NOTE: we shouldn't extrapolate an Avatar instance forward in time... 
    // until velocity is included in AvatarData update message.
    //_position += _velocity * deltaTime;
    measureMotionDerivatives(deltaTime);
}

void Avatar::slamPosition(const glm::vec3& newPosition) { 
    AvatarData::setPosition(newPosition);
    _positionDeltaAccumulator = glm::vec3(0.0f);
    _velocity = glm::vec3(0.0f);
    _lastVelocity = glm::vec3(0.0f); 
}

void Avatar::applyPositionDelta(const glm::vec3& delta) {
    _position += delta;
    _positionDeltaAccumulator += delta;
}

void Avatar::measureMotionDerivatives(float deltaTime) {
    // linear
    float invDeltaTime = 1.0f / deltaTime;
    // Floating point error prevents us from computing velocity in a naive way
    // (e.g. vel = (pos - oldPos) / dt) so instead we use _positionOffsetAccumulator.
    _velocity = _positionDeltaAccumulator * invDeltaTime;
    _positionDeltaAccumulator = glm::vec3(0.0f);
    _acceleration = (_velocity - _lastVelocity) * invDeltaTime;
    _lastVelocity = _velocity;
    // angular
    glm::quat orientation = getOrientation();
    glm::quat delta = glm::inverse(_lastOrientation) * orientation;
    _angularVelocity = safeEulerAngles(delta) * invDeltaTime;
    _angularAcceleration = (_angularVelocity - _lastAngularVelocity) * invDeltaTime;
    _lastOrientation = getOrientation();
}

enum TextRendererType {
    CHAT, 
    DISPLAYNAME
};

static TextRenderer* textRenderer(TextRendererType type) {
    static TextRenderer* chatRenderer = TextRenderer::getInstance(SANS_FONT_FAMILY, 24, -1,
        false, TextRenderer::SHADOW_EFFECT);
    static TextRenderer* displayNameRenderer = TextRenderer::getInstance(SANS_FONT_FAMILY, 12);

    switch(type) {
    case CHAT:
        return chatRenderer;
    case DISPLAYNAME:
        return displayNameRenderer;
    }

    return displayNameRenderer;
}

void Avatar::render(const glm::vec3& cameraPosition, RenderMode renderMode, bool postLighting) {
    if (_referential) {
        _referential->update();
    }
    
    if (postLighting && glm::distance(Application::getInstance()->getAvatar()->getPosition(), _position) < 10.0f) {
        auto geometryCache = DependencyManager::get<GeometryCache>();

        // render pointing lasers
        glm::vec3 laserColor = glm::vec3(1.0f, 0.0f, 1.0f);
        float laserLength = 50.0f;
        glm::vec3 position;
        glm::quat rotation;
        bool havePosition, haveRotation;

        if (_handState & LEFT_HAND_POINTING_FLAG) {

            if (_handState & IS_FINGER_POINTING_FLAG) {
                int leftIndexTip = getJointIndex("LeftHandIndex4");
                int leftIndexTipJoint = getJointIndex("LeftHandIndex3");
                havePosition = _skeletonModel.getJointPositionInWorldFrame(leftIndexTip, position);
                haveRotation = _skeletonModel.getJointRotationInWorldFrame(leftIndexTipJoint, rotation);
            } else {
                int leftHand = _skeletonModel.getLeftHandJointIndex();
                havePosition = _skeletonModel.getJointPositionInWorldFrame(leftHand, position);
                haveRotation = _skeletonModel.getJointRotationInWorldFrame(leftHand, rotation);
            }

            if (havePosition && haveRotation) {
                glPushMatrix(); {
                    glTranslatef(position.x, position.y, position.z);
                    float angle = glm::degrees(glm::angle(rotation));
                    glm::vec3 axis = glm::axis(rotation);
                    glRotatef(angle, axis.x, axis.y, axis.z);
                    
                    geometryCache->renderLine(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, laserLength, 0.0f), laserColor);
                    
                } glPopMatrix();
            }
        }

        if (_handState & RIGHT_HAND_POINTING_FLAG) {
            
            if (_handState & IS_FINGER_POINTING_FLAG) {
                int rightIndexTip = getJointIndex("RightHandIndex4");
                int rightIndexTipJoint = getJointIndex("RightHandIndex3");
                havePosition = _skeletonModel.getJointPositionInWorldFrame(rightIndexTip, position);
                haveRotation = _skeletonModel.getJointRotationInWorldFrame(rightIndexTipJoint, rotation);
            } else {
                int rightHand = _skeletonModel.getRightHandJointIndex();
                havePosition = _skeletonModel.getJointPositionInWorldFrame(rightHand, position);
                haveRotation = _skeletonModel.getJointRotationInWorldFrame(rightHand, rotation);
            }

            if (havePosition && haveRotation) {
                glPushMatrix(); {
                    glTranslatef(position.x, position.y, position.z);
                    float angle = glm::degrees(glm::angle(rotation));
                    glm::vec3 axis = glm::axis(rotation);
                    glRotatef(angle, axis.x, axis.y, axis.z);
                    geometryCache->renderLine(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, laserLength, 0.0f), laserColor);
                    
                } glPopMatrix();
            }
        }
    }
    
    // simple frustum check
    float boundingRadius = getBillboardSize();
    ViewFrustum* frustum = (renderMode == Avatar::SHADOW_RENDER_MODE) ?
        Application::getInstance()->getShadowViewFrustum() : Application::getInstance()->getViewFrustum();
    if (frustum->sphereInFrustum(getPosition(), boundingRadius) == ViewFrustum::OUTSIDE) {
        return;
    }

    glm::vec3 toTarget = cameraPosition - getPosition();
    float distanceToTarget = glm::length(toTarget);
   
    {
        // glow when moving far away
        const float GLOW_DISTANCE = 20.0f;
        const float GLOW_MAX_LOUDNESS = 2500.0f;
        const float MAX_GLOW = 0.5f;
     
        float GLOW_FROM_AVERAGE_LOUDNESS = ((this == Application::getInstance()->getAvatar())
                                            ? 0.0f
                                            : MAX_GLOW * getHeadData()->getAudioLoudness() / GLOW_MAX_LOUDNESS);
        if (!Menu::getInstance()->isOptionChecked(MenuOption::GlowWhenSpeaking)) {
            GLOW_FROM_AVERAGE_LOUDNESS = 0.0f;
        }
            
        float glowLevel = _moving && distanceToTarget > GLOW_DISTANCE && renderMode == NORMAL_RENDER_MODE
                      ? 1.0f
                      : GLOW_FROM_AVERAGE_LOUDNESS;
        
        // render body
        if (Menu::getInstance()->isOptionChecked(MenuOption::Avatars)) {
            renderBody(renderMode, postLighting, glowLevel);
        }

        if (!postLighting && renderMode != SHADOW_RENDER_MODE) {
            // add local lights
            const float BASE_LIGHT_DISTANCE = 2.0f;
            const float LIGHT_EXPONENT = 1.0f;
            const float LIGHT_CUTOFF = glm::radians(80.0f);
            float distance = BASE_LIGHT_DISTANCE * _scale;
            glm::vec3 position = glm::mix(_skeletonModel.getTranslation(), getHead()->getFaceModel().getTranslation(), 0.9f);
            glm::quat orientation = getOrientation();
            foreach (const AvatarManager::LocalLight& light, Application::getInstance()->getAvatarManager().getLocalLights()) {
                glm::vec3 direction = orientation * light.direction;
                DependencyManager::get<DeferredLightingEffect>()->addSpotLight(position - direction * distance,
                    distance * 2.0f, glm::vec3(), light.color, light.color, 1.0f, 0.5f, 0.0f, direction,
                    LIGHT_EXPONENT, LIGHT_CUTOFF);
            }
        }
        
        if (postLighting) {
            bool renderSkeleton = Menu::getInstance()->isOptionChecked(MenuOption::RenderSkeletonCollisionShapes);
            bool renderHead = Menu::getInstance()->isOptionChecked(MenuOption::RenderHeadCollisionShapes);
            bool renderBounding = Menu::getInstance()->isOptionChecked(MenuOption::RenderBoundingCollisionShapes);

            if (renderSkeleton) {
                _skeletonModel.renderJointCollisionShapes(0.7f);
            }

            if (renderHead && shouldRenderHead(cameraPosition, renderMode)) {
                getHead()->getFaceModel().renderJointCollisionShapes(0.7f);
            }
            if (renderBounding && shouldRenderHead(cameraPosition, renderMode)) {
                _skeletonModel.renderBoundingCollisionShapes(0.7f);
            }

            // If this is the avatar being looked at, render a little ball above their head
            if (_isLookAtTarget && Menu::getInstance()->isOptionChecked(MenuOption::RenderFocusIndicator)) {
                const float LOOK_AT_INDICATOR_RADIUS = 0.03f;
                const float LOOK_AT_INDICATOR_OFFSET = 0.22f;
                const glm::vec4 LOOK_AT_INDICATOR_COLOR = { 0.8f, 0.0f, 0.0f, 0.75f };
                glPushMatrix();
                if (_displayName.isEmpty() || _displayNameAlpha == 0.0f) {
                    glTranslatef(_position.x, getDisplayNamePosition().y, _position.z);
                } else {
                    glTranslatef(_position.x, getDisplayNamePosition().y + LOOK_AT_INDICATOR_OFFSET, _position.z);
                }
                DependencyManager::get<GeometryCache>()->renderSphere(LOOK_AT_INDICATOR_RADIUS, 15, 15, LOOK_AT_INDICATOR_COLOR);
                glPopMatrix();
            }
        }

        // quick check before falling into the code below:
        // (a 10 degree breadth of an almost 2 meter avatar kicks in at about 12m)
        const float MIN_VOICE_SPHERE_DISTANCE = 12.0f;
        if (postLighting && Menu::getInstance()->isOptionChecked(MenuOption::BlueSpeechSphere)
            && distanceToTarget > MIN_VOICE_SPHERE_DISTANCE) {

            // render voice intensity sphere for avatars that are farther away
            const float MAX_SPHERE_ANGLE = 10.0f * RADIANS_PER_DEGREE;
            const float MIN_SPHERE_ANGLE = 0.5f * RADIANS_PER_DEGREE;
            const float MIN_SPHERE_SIZE = 0.01f;
            const float SPHERE_LOUDNESS_SCALING = 0.0005f;
            const float SPHERE_COLOR[] = { 0.5f, 0.8f, 0.8f };
            float height = getSkeletonHeight();
            glm::vec3 delta = height * (getHead()->getCameraOrientation() * IDENTITY_UP) / 2.0f;
            float angle = abs(angleBetween(toTarget + delta, toTarget - delta));
            float sphereRadius = getHead()->getAverageLoudness() * SPHERE_LOUDNESS_SCALING;
            
            if (renderMode == NORMAL_RENDER_MODE && (sphereRadius > MIN_SPHERE_SIZE) &&
                    (angle < MAX_SPHERE_ANGLE) && (angle > MIN_SPHERE_ANGLE)) {
                glPushMatrix();
                glTranslatef(_position.x, _position.y, _position.z);
                glScalef(height, height, height);
                DependencyManager::get<GeometryCache>()->renderSphere(sphereRadius, 15, 15,
                                        glm::vec4(SPHERE_COLOR[0], SPHERE_COLOR[1], SPHERE_COLOR[2], 1.0f - angle / MAX_SPHERE_ANGLE));

                glPopMatrix();
            }
        }
    }

    const float DISPLAYNAME_DISTANCE = 20.0f;
    setShowDisplayName(renderMode == NORMAL_RENDER_MODE && distanceToTarget < DISPLAYNAME_DISTANCE);
    if (!postLighting || renderMode != NORMAL_RENDER_MODE || (isMyAvatar() &&
            Application::getInstance()->getCamera()->getMode() == CAMERA_MODE_FIRST_PERSON)) {
        return;
    }
    renderDisplayName();
}

glm::quat Avatar::computeRotationFromBodyToWorldUp(float proportion) const {
    glm::quat orientation = getOrientation();
    glm::vec3 currentUp = orientation * IDENTITY_UP;
    float angle = acosf(glm::clamp(glm::dot(currentUp, _worldUpDirection), -1.0f, 1.0f));
    if (angle < EPSILON) {
        return glm::quat();
    }
    glm::vec3 axis;
    if (angle > 179.99f * RADIANS_PER_DEGREE) { // 180 degree rotation; must use another axis
        axis = orientation * IDENTITY_RIGHT;
    } else {
        axis = glm::normalize(glm::cross(currentUp, _worldUpDirection));
    }
    return glm::angleAxis(angle * proportion, axis);
}

void Avatar::renderBody(RenderMode renderMode, bool postLighting, float glowLevel) {
    Model::RenderMode modelRenderMode = (renderMode == SHADOW_RENDER_MODE) ?
                            Model::SHADOW_RENDER_MODE : Model::DEFAULT_RENDER_MODE;
    {
        Glower glower(glowLevel);
        
        if (_shouldRenderBillboard || !(_skeletonModel.isRenderable() && getHead()->getFaceModel().isRenderable())) {
            if (postLighting || renderMode == SHADOW_RENDER_MODE) {
                // render the billboard until both models are loaded
                renderBillboard();
            }
            return;
        }
        
        if (postLighting) {
            getHand()->render(false, modelRenderMode);
        } else {
            _skeletonModel.render(1.0f, modelRenderMode);
            renderAttachments(renderMode);
        }
    }
    getHead()->render(1.0f, modelRenderMode, postLighting);
}

bool Avatar::shouldRenderHead(const glm::vec3& cameraPosition, RenderMode renderMode) const {
    return true;
}

void Avatar::simulateAttachments(float deltaTime) {
    for (int i = 0; i < _attachmentModels.size(); i++) {
        const AttachmentData& attachment = _attachmentData.at(i);
        Model* model = _attachmentModels.at(i);
        int jointIndex = getJointIndex(attachment.jointName);
        glm::vec3 jointPosition;
        glm::quat jointRotation;
        if (!isMyAvatar()) {
            model->setLODDistance(getLODDistance());
        }
        if (_skeletonModel.getJointPositionInWorldFrame(jointIndex, jointPosition) &&
                _skeletonModel.getJointCombinedRotation(jointIndex, jointRotation)) {
            model->setTranslation(jointPosition + jointRotation * attachment.translation * _scale);
            model->setRotation(jointRotation * attachment.rotation);
            model->setScaleToFit(true, _scale * attachment.scale);
            model->simulate(deltaTime);
        }
    }
}

void Avatar::renderAttachments(RenderMode renderMode) {
    Model::RenderMode modelRenderMode = (renderMode == SHADOW_RENDER_MODE) ?
        Model::SHADOW_RENDER_MODE : Model::DEFAULT_RENDER_MODE;
    foreach (Model* model, _attachmentModels) {
        model->render(1.0f, modelRenderMode);
    }
}

void Avatar::updateJointMappings() {
    // no-op; joint mappings come from skeleton model
}

void Avatar::renderBillboard() {
    if (_billboard.isEmpty()) {
        return;
    }
    if (!_billboardTexture) {
        QImage image = QImage::fromData(_billboard);
        if (image.format() != QImage::Format_ARGB32) {
            image = image.convertToFormat(QImage::Format_ARGB32);
        }
        _billboardTexture.reset(new Texture());
        glBindTexture(GL_TEXTURE_2D, _billboardTexture->getID());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0,
            GL_BGRA, GL_UNSIGNED_BYTE, image.constBits());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    } else {
        glBindTexture(GL_TEXTURE_2D, _billboardTexture->getID());
    }
    
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    
    glPushMatrix();
    glTranslatef(_position.x, _position.y, _position.z);
    
    // rotate about vertical to face the camera
    glm::quat rotation = getOrientation();
    glm::vec3 cameraVector = glm::inverse(rotation) * (Application::getInstance()->getCamera()->getPosition() - _position);
    rotation = rotation * glm::angleAxis(atan2f(-cameraVector.x, -cameraVector.z), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 axis = glm::axis(rotation);
    glRotatef(glm::degrees(glm::angle(rotation)), axis.x, axis.y, axis.z);
    
    // compute the size from the billboard camera parameters and scale
    float size = getBillboardSize();
    glScalef(size, size, size);
    
    glm::vec2 topLeft(-1.0f, -1.0f);
    glm::vec2 bottomRight(1.0f, 1.0f);
    glm::vec2 texCoordTopLeft(0.0f, 0.0f);
    glm::vec2 texCoordBottomRight(1.0f, 1.0f);

    DependencyManager::get<GeometryCache>()->renderQuad(topLeft, bottomRight, texCoordTopLeft, texCoordBottomRight, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    
    glPopMatrix();
    
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    
    glBindTexture(GL_TEXTURE_2D, 0);
}

float Avatar::getBillboardSize() const {
    return _scale * BILLBOARD_DISTANCE * tanf(glm::radians(BILLBOARD_FIELD_OF_VIEW / 2.0f));
}

glm::vec3 Avatar::getDisplayNamePosition() {
    glm::vec3 namePosition;
    if (getSkeletonModel().getNeckPosition(namePosition)) {
        namePosition += getBodyUpDirection() * getHeadHeight() * 1.1f;
    } else {
        const float HEAD_PROPORTION = 0.75f;
        namePosition = _position + getBodyUpDirection() * (getBillboardSize() * HEAD_PROPORTION);
    }
    return namePosition;
}

float Avatar::calculateDisplayNameScaleFactor(const glm::vec3& textPosition, bool inHMD) {

    // We need to compute the scale factor such as the text remains with fixed size respect to window coordinates
    // We project a unit vector and check the difference in screen coordinates, to check which is the 
    // correction scale needed
    // save the matrices for later scale correction factor 
    // The up vector must be relative to the rotation current rotation matrix:
    // we set the identity
    glm::vec3 testPoint0 = textPosition;
    glm::vec3 testPoint1 = textPosition + (Application::getInstance()->getCamera()->getRotation() * IDENTITY_UP);
    
    double textWindowHeight;
    
    GLint viewportMatrix[4];
    glGetIntegerv(GL_VIEWPORT, viewportMatrix);
    glm::dmat4 modelViewMatrix;
    float windowSizeX = viewportMatrix[2] - viewportMatrix[0];
    float windowSizeY = viewportMatrix[3] - viewportMatrix[1];
    
    glm::dmat4 projectionMatrix;
    Application::getInstance()->getModelViewMatrix(&modelViewMatrix);
    Application::getInstance()->getProjectionMatrix(&projectionMatrix);
    

    glm::dvec4 p0 = modelViewMatrix * glm::dvec4(testPoint0, 1.0);
    p0 = projectionMatrix * p0;
    glm::dvec2 result0 = glm::vec2(windowSizeX * (p0.x / p0.w + 1.0f) * 0.5f, windowSizeY * (p0.y / p0.w + 1.0f) * 0.5f);

    glm::dvec4 p1 = modelViewMatrix * glm::dvec4(testPoint1, 1.0);
    p1 = projectionMatrix * p1;
    glm::vec2 result1 = glm::vec2(windowSizeX * (p1.x / p1.w + 1.0f) * 0.5f, windowSizeY * (p1.y / p1.w + 1.0f) * 0.5f);
    textWindowHeight = abs(result1.y - result0.y);

    // need to scale to compensate for the font resolution due to the device
    float scaleFactor = QApplication::desktop()->windowHandle()->devicePixelRatio() *
        ((textWindowHeight > EPSILON) ? 1.0f / textWindowHeight : 1.0f);
    if (inHMD) {
        const float HMDMODE_NAME_SCALE = 0.65f;
        scaleFactor *= HMDMODE_NAME_SCALE;
    } else {
        scaleFactor *= Application::getInstance()->getRenderResolutionScale();
    }
    return scaleFactor;
}

void Avatar::renderDisplayName() {

    if (_displayName.isEmpty() || _displayNameAlpha == 0.0f) {
        return;
    }
    
    // which viewing mode?
    bool inHMD = Application::getInstance()->isHMDMode();
    
    glDisable(GL_LIGHTING);
    
    glPushMatrix();
    glm::vec3 textPosition = getDisplayNamePosition();
    
    glTranslatef(textPosition.x, textPosition.y, textPosition.z); 

    // we need "always facing camera": we must remove the camera rotation from the stack

    
    glm::vec3 frontAxis(0.0f, 0.0f, 1.0f);
    if (inHMD) {
        glm::vec3 camPosition = Application::getInstance()->getCamera()->getPosition();
        frontAxis = camPosition - textPosition;
    } else {
        glm::quat rotation = Application::getInstance()->getCamera()->getRotation();
        frontAxis = glm::rotate(rotation, frontAxis);
    }
    
    frontAxis = glm::normalize(glm::vec3(frontAxis.z, 0.0f, -frontAxis.x));
    float angle = acos(frontAxis.x) * ((frontAxis.z < 0) ? 1.0f : -1.0f);
    glRotatef(glm::degrees(angle), 0.0f, 1.0f, 0.0f);
    
    float scaleFactor = calculateDisplayNameScaleFactor(textPosition, inHMD);
    glScalef(scaleFactor, scaleFactor, 1.0);
    
    glScalef(1.0f, -1.0f, 1.0f);  // TextRenderer::draw paints the text upside down in y axis

    int text_x = -_displayNameBoundingRect.width() / 2;
    int text_y = -_displayNameBoundingRect.height() / 2;

    // draw a gray background
    int left = text_x + _displayNameBoundingRect.x();
    int right = left + _displayNameBoundingRect.width();
    int bottom = text_y + _displayNameBoundingRect.y();
    int top = bottom + _displayNameBoundingRect.height();
    const int border = 8;
    bottom -= border;
    left -= border;
    top += border;
    right += border;

    // We are drawing coplanar textures with depth: need the polygon offset
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    DependencyManager::get<GeometryCache>()->renderBevelCornersRect(left, bottom, right - left, top - bottom, 3,
            glm::vec4(0.2f, 0.2f, 0.2f, _displayNameAlpha * DISPLAYNAME_BACKGROUND_ALPHA / DISPLAYNAME_ALPHA));
   
    glm::vec4 color(0.93f, 0.93f, 0.93f, _displayNameAlpha);
    QByteArray ba = _displayName.toLocal8Bit();
    const char* text = ba.data();
    
    glDisable(GL_POLYGON_OFFSET_FILL);
    textRenderer(DISPLAYNAME)->draw(text_x, text_y, text, color);

    glPopMatrix();

    glEnable(GL_LIGHTING);
}

bool Avatar::findRayIntersection(RayIntersectionInfo& intersection) const {
    bool hit = _skeletonModel.findRayIntersection(intersection);
    hit = getHead()->getFaceModel().findRayIntersection(intersection) || hit;
    return hit;
}

bool Avatar::findSphereCollisions(const glm::vec3& penetratorCenter, float penetratorRadius, CollisionList& collisions) {
    return _skeletonModel.findSphereCollisions(penetratorCenter, penetratorRadius, collisions);
    // TODO: Andrew to fix: Temporarily disabling collisions against the head
    //return getHead()->getFaceModel().findSphereCollisions(penetratorCenter, penetratorRadius, collisions);
}

bool Avatar::findPlaneCollisions(const glm::vec4& plane, CollisionList& collisions) {
    return _skeletonModel.findPlaneCollisions(plane, collisions) ||
        getHead()->getFaceModel().findPlaneCollisions(plane, collisions);
}

bool Avatar::findCollisions(const QVector<const Shape*>& shapes, CollisionList& collisions) {
    // TODO: Andrew to fix: also collide against _skeleton
    //bool collided = _skeletonModel.findCollisions(shapes, collisions);

    Model& headModel = getHead()->getFaceModel();
    //collided = headModel.findCollisions(shapes, collisions) || collided;
    bool collided = headModel.findCollisions(shapes, collisions);
    return collided;
}

void Avatar::setSkeletonOffset(const glm::vec3& offset) {
    const float MAX_OFFSET_LENGTH = _scale * 0.5f;
    float offsetLength = glm::length(offset);
    if (offsetLength > MAX_OFFSET_LENGTH) {
        _skeletonOffset = (MAX_OFFSET_LENGTH / offsetLength) * offset;
    } else {
        _skeletonOffset = offset;
    }
}

glm::vec3 Avatar::getSkeletonPosition() const { 
    // The avatar is rotated PI about the yAxis, so we have to correct for it 
    // to get the skeleton offset contribution in the world-frame.
    const glm::quat FLIP = glm::angleAxis(PI, glm::vec3(0.0f, 1.0f, 0.0f));
    return _position + getOrientation() * FLIP * _skeletonOffset; 
}

QVector<glm::quat> Avatar::getJointRotations() const {
    if (QThread::currentThread() != thread()) {
        return AvatarData::getJointRotations();
    }
    QVector<glm::quat> jointRotations(_skeletonModel.getJointStateCount());
    for (int i = 0; i < _skeletonModel.getJointStateCount(); ++i) {
        _skeletonModel.getJointState(i, jointRotations[i]);
    }
    return jointRotations;
}

glm::quat Avatar::getJointRotation(int index) const {
    if (QThread::currentThread() != thread()) {
        return AvatarData::getJointRotation(index);
    }
    glm::quat rotation;
    _skeletonModel.getJointState(index, rotation);
    return rotation;
}

int Avatar::getJointIndex(const QString& name) const {
    if (QThread::currentThread() != thread()) {
        int result;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointIndex", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(int, result), Q_ARG(const QString&, name));
        return result;
    }
    return _skeletonModel.isActive() ? _skeletonModel.getGeometry()->getFBXGeometry().getJointIndex(name) : -1;
}

QStringList Avatar::getJointNames() const {
    if (QThread::currentThread() != thread()) {
        QStringList result;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointNames", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(QStringList, result));
        return result;
    }
    return _skeletonModel.isActive() ? _skeletonModel.getGeometry()->getFBXGeometry().getJointNames() : QStringList();
}

glm::vec3 Avatar::getJointPosition(int index) const {
    if (QThread::currentThread() != thread()) {
        glm::vec3 position;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointPosition", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(glm::vec3, position), Q_ARG(const int, index));
        return position;
    }
    glm::vec3 position;
    _skeletonModel.getJointPositionInWorldFrame(index, position);
    return position;
}

glm::vec3 Avatar::getJointPosition(const QString& name) const {
    if (QThread::currentThread() != thread()) {
        glm::vec3 position;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointPosition", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(glm::vec3, position), Q_ARG(const QString&, name));
        return position;
    }
    glm::vec3 position;
    _skeletonModel.getJointPositionInWorldFrame(getJointIndex(name), position);
    return position;
}

glm::quat Avatar::getJointCombinedRotation(int index) const {
    if (QThread::currentThread() != thread()) {
        glm::quat rotation;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointCombinedRotation", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(glm::quat, rotation), Q_ARG(const int, index));
        return rotation;
    }
    glm::quat rotation;
    _skeletonModel.getJointCombinedRotation(index, rotation);
    return rotation;
}

glm::quat Avatar::getJointCombinedRotation(const QString& name) const {
    if (QThread::currentThread() != thread()) {
        glm::quat rotation;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointCombinedRotation", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(glm::quat, rotation), Q_ARG(const QString&, name));
        return rotation;
    }
    glm::quat rotation;
    _skeletonModel.getJointCombinedRotation(getJointIndex(name), rotation);
    return rotation;
}

const float SCRIPT_PRIORITY = DEFAULT_PRIORITY + 1.0f;

void Avatar::setJointModelPositionAndOrientation(int index, glm::vec3 position, const glm::quat& rotation) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "setJointModelPositionAndOrientation", 
            Qt::AutoConnection, Q_ARG(const int, index), Q_ARG(const glm::vec3, position),
            Q_ARG(const glm::quat&, rotation));
    } else {
        _skeletonModel.inverseKinematics(index, position, rotation, SCRIPT_PRIORITY);
    }
}

void Avatar::setJointModelPositionAndOrientation(const QString& name, glm::vec3 position, const glm::quat& rotation) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "setJointModelPositionAndOrientation", 
            Qt::AutoConnection, Q_ARG(const QString&, name), Q_ARG(const glm::vec3, position),
            Q_ARG(const glm::quat&, rotation));
    } else {
        _skeletonModel.inverseKinematics(getJointIndex(name), position, rotation, SCRIPT_PRIORITY);
    }
}

void Avatar::scaleVectorRelativeToPosition(glm::vec3 &positionToScale) const {
    //Scale a world space vector as if it was relative to the position
    positionToScale = _position + _scale * (positionToScale - _position);
}

void Avatar::setFaceModelURL(const QUrl& faceModelURL) {
    AvatarData::setFaceModelURL(faceModelURL);
    const QUrl DEFAULT_FACE_MODEL_URL = QUrl::fromLocalFile(PathUtils::resourcesPath() + "meshes/defaultAvatar_head.fst");
    getHead()->getFaceModel().setURL(_faceModelURL, DEFAULT_FACE_MODEL_URL, true, !isMyAvatar());
}

void Avatar::setSkeletonModelURL(const QUrl& skeletonModelURL) {
    AvatarData::setSkeletonModelURL(skeletonModelURL);
    const QUrl DEFAULT_SKELETON_MODEL_URL = QUrl::fromLocalFile(PathUtils::resourcesPath() + "meshes/defaultAvatar_body.fst");
    _skeletonModel.setURL(_skeletonModelURL, DEFAULT_SKELETON_MODEL_URL, true, !isMyAvatar());
}

void Avatar::setAttachmentData(const QVector<AttachmentData>& attachmentData) {
    AvatarData::setAttachmentData(attachmentData);
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "setAttachmentData", Qt::DirectConnection,
                                  Q_ARG(const QVector<AttachmentData>, attachmentData));
        return;
    }
    // make sure we have as many models as attachments
    while (_attachmentModels.size() < attachmentData.size()) {
        Model* model = new Model(this);
        model->init();
        _attachmentModels.append(model);
    }
    while (_attachmentModels.size() > attachmentData.size()) {
        delete _attachmentModels.takeLast();
    }
    
    // update the urls
    for (int i = 0; i < attachmentData.size(); i++) {
        _attachmentModels[i]->setURL(attachmentData.at(i).modelURL);
        _attachmentModels[i]->setSnapModelToCenter(true);
        _attachmentModels[i]->setScaleToFit(true, _scale * _attachmentData.at(i).scale);
    }
}

void Avatar::setDisplayName(const QString& displayName) {
    AvatarData::setDisplayName(displayName);
    _displayNameBoundingRect = textRenderer(DISPLAYNAME)->metrics().tightBoundingRect(displayName);
}

void Avatar::setBillboard(const QByteArray& billboard) {
    AvatarData::setBillboard(billboard);
    
    // clear out any existing billboard texture
    _billboardTexture.reset();
}

int Avatar::parseDataAtOffset(const QByteArray& packet, int offset) {
    if (!_initialized) {
        // now that we have data for this Avatar we are go for init
        init();
    }
    
    // change in position implies movement
    glm::vec3 oldPosition = _position;
    
    int bytesRead = AvatarData::parseDataAtOffset(packet, offset);
    
    const float MOVE_DISTANCE_THRESHOLD = 0.001f;
    _moving = glm::distance(oldPosition, _position) > MOVE_DISTANCE_THRESHOLD;
    
    return bytesRead;
}

int Avatar::_jointConesID = GeometryCache::UNKNOWN_ID;

// render a makeshift cone section that serves as a body part connecting joint spheres
void Avatar::renderJointConnectingCone(glm::vec3 position1, glm::vec3 position2, 
                                            float radius1, float radius2, const glm::vec4& color) {
   
    auto geometryCache = DependencyManager::get<GeometryCache>();
    
    if (_jointConesID == GeometryCache::UNKNOWN_ID) {
        _jointConesID = geometryCache->allocateID();
    }
    
    glm::vec3 axis = position2 - position1;
    float length = glm::length(axis);
    
    if (length > 0.0f) {
        
        axis /= length;
        
        glm::vec3 perpSin = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 perpCos = glm::normalize(glm::cross(axis, perpSin));
        perpSin = glm::cross(perpCos, axis);
        
        float anglea = 0.0f;
        float angleb = 0.0f;
        QVector<glm::vec3> points;
        
        for (int i = 0; i < NUM_BODY_CONE_SIDES; i ++) {
            
            // the rectangles that comprise the sides of the cone section are
            // referenced by "a" and "b" in one dimension, and "1", and "2" in the other dimension.
            anglea = angleb;
            angleb = ((float)(i+1) / (float)NUM_BODY_CONE_SIDES) * TWO_PI;
            
            float sa = sinf(anglea);
            float sb = sinf(angleb);
            float ca = cosf(anglea);
            float cb = cosf(angleb);
            
            glm::vec3 p1a = position1 + perpSin * sa * radius1 + perpCos * ca * radius1;
            glm::vec3 p1b = position1 + perpSin * sb * radius1 + perpCos * cb * radius1; 
            glm::vec3 p2a = position2 + perpSin * sa * radius2 + perpCos * ca * radius2;   
            glm::vec3 p2b = position2 + perpSin * sb * radius2 + perpCos * cb * radius2;  
            
            points << p1a << p1b << p2a << p1b << p2a << p2b;
        }
        
        // TODO: this is really inefficient constantly recreating these vertices buffers. It would be
        // better if the avatars cached these buffers for each of the joints they are rendering
        geometryCache->updateVertices(_jointConesID, points, color);
        geometryCache->renderVertices(gpu::TRIANGLES, _jointConesID);
    }
}

void Avatar::updateCollisionGroups() {
    _collisionGroups = 0;
    if (Menu::getInstance()->isOptionChecked(MenuOption::CollideWithEnvironment)) {
        _collisionGroups |= COLLISION_GROUP_ENVIRONMENT;
    }
    if (Menu::getInstance()->isOptionChecked(MenuOption::CollideWithAvatars)) {
        _collisionGroups |= COLLISION_GROUP_AVATARS;
    }
}

void Avatar::setScale(float scale) {
    _scale = scale;

    if (_targetScale * (1.0f - RESCALING_TOLERANCE) < _scale &&
            _scale < _targetScale * (1.0f + RESCALING_TOLERANCE)) {
        _scale = _targetScale;
    }
}

float Avatar::getSkeletonHeight() const {
    Extents extents = _skeletonModel.getBindExtents();
    return extents.maximum.y - extents.minimum.y;
}

float Avatar::getHeadHeight() const {
    Extents extents = getHead()->getFaceModel().getBindExtents();
    if (!extents.isEmpty()) {
        return extents.maximum.y - extents.minimum.y;
    }
    glm::vec3 neckPosition;
    glm::vec3 headPosition;
    if (_skeletonModel.getNeckPosition(neckPosition) && _skeletonModel.getHeadPosition(headPosition)) {
        return glm::distance(neckPosition, headPosition);
    }
    const float DEFAULT_HEAD_HEIGHT = 0.1f;
    return DEFAULT_HEAD_HEIGHT;
}

float Avatar::getBoundingRadius() const {
    // TODO: also use head model when computing the avatar's bounding radius
    return _skeletonModel.getBoundingRadius();
}

float Avatar::getPelvisFloatingHeight() const {
    return -_skeletonModel.getBindExtents().minimum.y;
}

void Avatar::setShowDisplayName(bool showDisplayName) {
    if (!Menu::getInstance()->isOptionChecked(MenuOption::NamesAboveHeads)) {
        _displayNameAlpha = 0.0f;
        return;
    }
    
    // For myAvatar, the alpha update is not done (called in simulate for other avatars)
    if (Application::getInstance()->getAvatar() == this) {
        if (showDisplayName) {
            _displayNameAlpha = DISPLAYNAME_ALPHA;
        } else {
            _displayNameAlpha = 0.0f;
        }
    } 

    if (showDisplayName) {
        _displayNameTargetAlpha = DISPLAYNAME_ALPHA;
    } else {
        _displayNameTargetAlpha = 0.0f;
    }

}

