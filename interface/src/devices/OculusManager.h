//
//  OculusManager.h
//  interface/src/devices
//
//  Created by Stephen Birarda on 5/9/13.
//  Refactored by Ben Arnold on 6/30/2014
//  Copyright 2012 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_OculusManager_h
#define hifi_OculusManager_h

#ifdef HAVE_LIBOVR
#include <OVR.h>
#endif

#include <ProgramObject.h>

class Camera;
class PalmData;
class Text3DOverlay;

/// Handles interaction with the Oculus Rift.
class OculusManager {
public:
    static void connect();
    static void disconnect();
    static bool isConnected();
    static void recalibrate();
    static void abandonCalibration();
    static void beginFrameTiming();
    static void endFrameTiming();
    static void configureCamera(Camera& camera, int screenWidth, int screenHeight);
    static void display(const glm::quat &bodyOrientation, const glm::vec3 &position, Camera& whichCamera);
    static void reset();
    
    /// param \yaw[out] yaw in radians
    /// param \pitch[out] pitch in radians
    /// param \roll[out] roll in radians
    static void getEulerAngles(float& yaw, float& pitch, float& roll);
    static glm::vec3 getRelativePosition();
    static QSize getRenderTargetSize();
    
    static void overrideOffAxisFrustum(float& left, float& right, float& bottom, float& top, float& nearVal,
        float& farVal, glm::vec4& nearClipPlane, glm::vec4& farClipPlane);
    
    static glm::vec3 getLeftEyePosition() { return _leftEyePosition; }
    static glm::vec3 getRightEyePosition() { return _rightEyePosition; }
    
    static int getHMDScreen();
    
private:
#ifdef HAVE_LIBOVR
    static void generateDistortionMesh();
    static void renderDistortionMesh(ovrPosef eyeRenderPose[ovrEye_Count]);

    static bool similarNames(const QString& nameA,const QString& nameB);

    struct DistortionVertex {
        glm::vec2 pos;
        glm::vec2 texR;
        glm::vec2 texG;
        glm::vec2 texB;
        struct {
            GLubyte r;
            GLubyte g;
            GLubyte b;
            GLubyte a;
        } color;
    };

    static ProgramObject _program;
    //Uniforms
    static int _textureLocation;
    static int _eyeToSourceUVScaleLocation;
    static int _eyeToSourceUVOffsetLocation;
    static int _eyeRotationStartLocation;
    static int _eyeRotationEndLocation;
    //Attributes
    static int _positionAttributeLocation;
    static int _colorAttributeLocation;
    static int _texCoord0AttributeLocation;
    static int _texCoord1AttributeLocation;
    static int _texCoord2AttributeLocation;

    static bool _isConnected;
    
    static ovrHmd _ovrHmd;
    static ovrHmdDesc _ovrHmdDesc;
    static ovrFovPort _eyeFov[ovrEye_Count];
    static ovrEyeRenderDesc _eyeRenderDesc[ovrEye_Count];
    static ovrSizei _renderTargetSize;
    static ovrVector2f _UVScaleOffset[ovrEye_Count][2];
    static GLuint _vertices[ovrEye_Count];
    static GLuint _indices[ovrEye_Count];
    static GLsizei _meshSize[ovrEye_Count];
    static ovrFrameTiming _hmdFrameTiming;
    static ovrRecti _eyeRenderViewport[ovrEye_Count];
    static unsigned int _frameIndex;
    static bool _frameTimingActive;
    static bool _programInitialized;
    static Camera* _camera;
    static int _activeEyeIndex;

    static void calibrate(const glm::vec3 position, const glm::quat orientation);
    enum CalibrationState {
        UNCALIBRATED,
        WAITING_FOR_DELTA,
        WAITING_FOR_ZERO,
        WAITING_FOR_ZERO_HELD,
        CALIBRATED
    };
    static void positionCalibrationBillboard(Text3DOverlay* message);
    static float CALIBRATION_DELTA_MINIMUM_LENGTH;
    static float CALIBRATION_DELTA_MINIMUM_ANGLE;
    static float CALIBRATION_ZERO_MAXIMUM_LENGTH;
    static float CALIBRATION_ZERO_MAXIMUM_ANGLE;
    static quint64 CALIBRATION_ZERO_HOLD_TIME;
    static float CALIBRATION_MESSAGE_DISTANCE;
    static CalibrationState _calibrationState;
    static glm::vec3 _calibrationPosition;
    static glm::quat _calibrationOrientation;
    static quint64 _calibrationStartTime;
    static int _calibrationMessage;

#endif
    
    static glm::vec3 _leftEyePosition;
    static glm::vec3 _rightEyePosition;
    

};

#endif // hifi_OculusManager_h
