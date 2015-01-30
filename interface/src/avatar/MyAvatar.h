//
//  MyAvatar.h
//  interface/src/avatar
//
//  Created by Mark Peng on 8/16/13.
//  Copyright 2012 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MyAvatar_h
#define hifi_MyAvatar_h

#include <QSettings>

#include <PhysicsSimulation.h>

#include "Avatar.h"

class ModelItemID;

class MyAvatar : public Avatar {
    Q_OBJECT
    Q_PROPERTY(bool shouldRenderLocally READ getShouldRenderLocally WRITE setShouldRenderLocally)
    Q_PROPERTY(glm::vec3 motorVelocity READ getScriptedMotorVelocity WRITE setScriptedMotorVelocity)
    Q_PROPERTY(float motorTimescale READ getScriptedMotorTimescale WRITE setScriptedMotorTimescale)
    Q_PROPERTY(QString motorReferenceFrame READ getScriptedMotorFrame WRITE setScriptedMotorFrame)
    Q_PROPERTY(glm::vec3 gravity READ getGravity WRITE setLocalGravity)

public:
	MyAvatar();
    ~MyAvatar();

    QByteArray toByteArray();
    void reset();
    void update(float deltaTime);
    void simulate(float deltaTime);
    void updateFromTrackers(float deltaTime);

    void render(const glm::vec3& cameraPosition, RenderMode renderMode = NORMAL_RENDER_MODE, bool postLighting = false);
    void renderBody(RenderMode renderMode, bool postLighting, float glowLevel = 0.0f);
    bool shouldRenderHead(const glm::vec3& cameraPosition, RenderMode renderMode) const;
    void renderDebugBodyPoints();

    // setters
    void setLeanScale(float scale) { _leanScale = scale; }
    void setLocalGravity(glm::vec3 gravity);
    void setShouldRenderLocally(bool shouldRender) { _shouldRender = shouldRender; }

    // getters
    float getLeanScale() const { return _leanScale; }
    glm::vec3 getGravity() const { return _gravity; }
    Q_INVOKABLE glm::vec3 getDefaultEyePosition() const;
    bool getShouldRenderLocally() const { return _shouldRender; }
    
    const QList<AnimationHandlePointer>& getAnimationHandles() const { return _animationHandles; }
    AnimationHandlePointer addAnimationHandle();
    void removeAnimationHandle(const AnimationHandlePointer& handle);
    
    /// Allows scripts to run animations.
    Q_INVOKABLE void startAnimation(const QString& url, float fps = 30.0f, float priority = 1.0f, bool loop = false,
        bool hold = false, float firstFrame = 0.0f, float lastFrame = FLT_MAX, const QStringList& maskedJoints = QStringList());
    
    /// Stops an animation as identified by a URL.
    Q_INVOKABLE void stopAnimation(const QString& url);
    
    /// Starts an animation by its role, using the provided URL and parameters if the avatar doesn't have a custom
    /// animation for the role.
    Q_INVOKABLE void startAnimationByRole(const QString& role, const QString& url = QString(), float fps = 30.0f,
        float priority = 1.0f, bool loop = false, bool hold = false, float firstFrame = 0.0f,
        float lastFrame = FLT_MAX, const QStringList& maskedJoints = QStringList());
    
    /// Stops an animation identified by its role.
    Q_INVOKABLE void stopAnimationByRole(const QString& role);

    Q_INVOKABLE AnimationDetails getAnimationDetailsByRole(const QString& role);
    Q_INVOKABLE AnimationDetails getAnimationDetails(const QString& url);
    
    // get/set avatar data
    void saveData();
    void loadData();

    void saveAttachmentData(const AttachmentData& attachment) const;
    AttachmentData loadAttachmentData(const QUrl& modelURL, const QString& jointName = QString()) const;

    //  Set what driving keys are being pressed to control thrust levels
    void clearDriveKeys();
    void setDriveKeys(int key, float val) { _driveKeys[key] = val; };
    bool getDriveKeys(int key) { return _driveKeys[key] != 0.0f; };
    void jump() { _shouldJump = true; };
    
    bool isMyAvatar() { return true; }
    
    bool isLookingAtLeftEye();

    virtual int parseDataAtOffset(const QByteArray& packet, int offset);
    
    static void sendKillAvatar();
    
    Q_INVOKABLE glm::vec3 getTrackedHeadPosition() const { return _trackedHeadPosition; }
    Q_INVOKABLE glm::vec3 getHeadPosition() const { return getHead()->getPosition(); }
    Q_INVOKABLE float getHeadFinalYaw() const { return getHead()->getFinalYaw(); }
    Q_INVOKABLE float getHeadFinalRoll() const { return getHead()->getFinalRoll(); }
    Q_INVOKABLE float getHeadFinalPitch() const { return getHead()->getFinalPitch(); }
    Q_INVOKABLE float getHeadDeltaPitch() const { return getHead()->getDeltaPitch(); }
    
    Q_INVOKABLE glm::vec3 getEyePosition() const { return getHead()->getEyePosition(); }
    
    Q_INVOKABLE glm::vec3 getTargetAvatarPosition() const { return _targetAvatarPosition; }
    QWeakPointer<AvatarData> getLookAtTargetAvatar() const { return _lookAtTargetAvatar; }
    void updateLookAtTargetAvatar();
    void clearLookAtTargetAvatar();
    
    virtual void setJointRotations(QVector<glm::quat> jointRotations);
    virtual void setJointData(int index, const glm::quat& rotation);
    virtual void clearJointData(int index);
    virtual void clearJointsData();
    virtual void setFaceModelURL(const QUrl& faceModelURL);
    virtual void setSkeletonModelURL(const QUrl& skeletonModelURL);
    virtual void setAttachmentData(const QVector<AttachmentData>& attachmentData);

    virtual glm::vec3 getSkeletonPosition() const;
    
    void clearJointAnimationPriorities();

    glm::vec3 getScriptedMotorVelocity() const { return _scriptedMotorVelocity; }
    float getScriptedMotorTimescale() const { return _scriptedMotorTimescale; }
    QString getScriptedMotorFrame() const;

    void setScriptedMotorVelocity(const glm::vec3& velocity);
    void setScriptedMotorTimescale(float timescale);
    void setScriptedMotorFrame(QString frame);

    void clearScriptableSettings();

    virtual void attach(const QString& modelURL, const QString& jointName = QString(),
        const glm::vec3& translation = glm::vec3(), const glm::quat& rotation = glm::quat(), float scale = 1.0f,
        bool allowDuplicates = false, bool useSaved = true);
        
    virtual void setCollisionGroups(quint32 collisionGroups);

    void applyCollision(const glm::vec3& contactPoint, const glm::vec3& penetration);

    /// Renders a laser pointer for UI picking
    void renderLaserPointers();
    glm::vec3 getLaserPointerTipPosition(const PalmData* palm);
    
    const RecorderPointer getRecorder() const { return _recorder; }
    const PlayerPointer getPlayer() const { return _player; }
    
public slots:
    void increaseSize();
    void decreaseSize();
    void resetSize();
    
    void goToLocation(const glm::vec3& newPosition,
                      bool hasOrientation = false, const glm::quat& newOrientation = glm::quat(),
                      bool shouldFaceLocation = false);

    //  Set/Get update the thrust that will move the avatar around
    void addThrust(glm::vec3 newThrust) { _thrust += newThrust; };
    glm::vec3 getThrust() { return _thrust; };
    void setThrust(glm::vec3 newThrust) { _thrust = newThrust; }

    void setVelocity(const glm::vec3 velocity) { _velocity = velocity; }

    void updateMotionBehavior();
    void onToggleRagdoll();
    
    glm::vec3 getLeftPalmPosition();
    glm::vec3 getRightPalmPosition();
    
    void clearReferential();
    bool setModelReferential(const QUuid& id);
    bool setJointReferential(const QUuid& id, int jointIndex);
    
    bool isRecording();
    qint64 recorderElapsed();
    void startRecording();
    void stopRecording();
    void saveRecording(QString filename);
    void loadLastRecording();
    
signals:
    void transformChanged();

protected:
    virtual void renderAttachments(RenderMode renderMode);
    
private:
    float _turningKeyPressTime;
    glm::vec3 _gravity;
    float _distanceToNearestAvatar; // How close is the nearest avatar?

    bool _shouldJump;
    float _driveKeys[MAX_DRIVE_KEYS];
    bool _wasPushing;
    bool _isPushing;
    bool _isBraking;

    float _trapDuration; // seconds that avatar has been trapped by collisions
    glm::vec3 _thrust;  // impulse accumulator for outside sources

    glm::vec3 _keyboardMotorVelocity; // target local-frame velocity of avatar (keyboard)
    float _keyboardMotorTimescale; // timescale for avatar to achieve its target velocity
    glm::vec3 _scriptedMotorVelocity; // target local-frame velocity of avatar (script)
    float _scriptedMotorTimescale; // timescale for avatar to achieve its target velocity
    int _scriptedMotorFrame;
    quint32 _motionBehaviors;

    QWeakPointer<AvatarData> _lookAtTargetAvatar;
    glm::vec3 _targetAvatarPosition;
    bool _shouldRender;
    bool _billboardValid;
    float _oculusYawOffset;

    QList<AnimationHandlePointer> _animationHandles;
    PhysicsSimulation _physicsSimulation;
    
    bool _feetTouchFloor;
    bool _isLookingAtLeftEye;

    RecorderPointer _recorder;
    
    glm::vec3 _trackedHeadPosition;
    
	// private methods
    void updateOrientation(float deltaTime);
    glm::vec3 applyKeyboardMotor(float deltaTime, const glm::vec3& velocity, bool walkingOnFloor);
    glm::vec3 applyScriptedMotor(float deltaTime, const glm::vec3& velocity);
    void updatePosition(float deltaTime);
    void updateCollisionWithAvatars(float deltaTime);
    void updateCollisionWithEnvironment(float deltaTime, float radius);
    void updateCollisionWithVoxels(float deltaTime, float radius);
    void applyHardCollision(const glm::vec3& penetration, float elasticity, float damping);
    void updateCollisionSound(const glm::vec3& penetration, float deltaTime, float frequency);
    void updateChatCircle(float deltaTime);
    void maybeUpdateBillboard();
    void setGravity(const glm::vec3& gravity);
};

#endif // hifi_MyAvatar_h
