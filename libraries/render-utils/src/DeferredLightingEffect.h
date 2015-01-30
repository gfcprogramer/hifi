//
//  DeferredLightingEffect.h
//  interface/src/renderer
//
//  Created by Andrzej Kapolka on 9/11/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_DeferredLightingEffect_h
#define hifi_DeferredLightingEffect_h

#include <QVector>

#include <DependencyManager.h>
#include <SharedUtil.h>

#include "ProgramObject.h"

class AbstractViewStateInterface;
class PostLightingRenderable;

/// Handles deferred lighting for the bits that require it (voxels, metavoxels...)
class DeferredLightingEffect : public Dependency {
    SINGLETON_DEPENDENCY
    
public:
    
    void init(AbstractViewStateInterface* viewState);

    /// Returns a reference to a simple program suitable for rendering static untextured geometry
    ProgramObject& getSimpleProgram() { return _simpleProgram; }

    /// Sets up the state necessary to render static untextured geometry with the simple program.
    void bindSimpleProgram();
    
    /// Tears down the state necessary to render static untextured geometry with the simple program.
    void releaseSimpleProgram();

    //// Renders a solid sphere with the simple program.
    void renderSolidSphere(float radius, int slices, int stacks, const glm::vec4& color);

    //// Renders a wireframe sphere with the simple program.
    void renderWireSphere(float radius, int slices, int stacks, const glm::vec4& color);
    
    //// Renders a solid cube with the simple program.
    void renderSolidCube(float size, const glm::vec4& color);

    //// Renders a wireframe cube with the simple program.
    void renderWireCube(float size, const glm::vec4& color);

    //// Renders a solid cone with the simple program.
    void renderSolidCone(float base, float height, int slices, int stacks);
    
    /// Adds a point light to render for the current frame.
    void addPointLight(const glm::vec3& position, float radius, const glm::vec3& ambient = glm::vec3(0.0f, 0.0f, 0.0f),
        const glm::vec3& diffuse = glm::vec3(1.0f, 1.0f, 1.0f), const glm::vec3& specular = glm::vec3(1.0f, 1.0f, 1.0f),
        float constantAttenuation = 1.0f, float linearAttenuation = 0.0f, float quadraticAttenuation = 0.0f);
        
    /// Adds a spot light to render for the current frame.
    void addSpotLight(const glm::vec3& position, float radius, const glm::vec3& ambient = glm::vec3(0.0f, 0.0f, 0.0f),
        const glm::vec3& diffuse = glm::vec3(1.0f, 1.0f, 1.0f), const glm::vec3& specular = glm::vec3(1.0f, 1.0f, 1.0f),
        float constantAttenuation = 1.0f, float linearAttenuation = 0.0f, float quadraticAttenuation = 0.0f,
        const glm::vec3& direction = glm::vec3(0.0f, 0.0f, -1.0f), float exponent = 0.0f, float cutoff = PI);
    
    /// Adds an object to render after performing the deferred lighting for the current frame (e.g., a translucent object).
    void addPostLightingRenderable(PostLightingRenderable* renderable) { _postLightingRenderables.append(renderable); }
    
    void prepare();
    void render();

    enum AmbientLightPreset {
        OLD_TOWN_SQUARE = 0,
        GRACE_CATHEDRAL,
        EUCALYPTUS_GROVE,
        ST_PETERS_BASILICA,
        UFFIZI_GALLERY,
        GALILEOS_TOMB,
        VINE_STREET_KITCHEN,
        BREEZEWAY,
        CAMPUS_SUNSET,
        FUNSTON_BEACH_SUNSET,

        NUM_PRESET,
    };

    void setAmbientLightMode(int preset);

private:
    DeferredLightingEffect() { }
    virtual ~DeferredLightingEffect() { }

    class LightLocations {
    public:
        int shadowDistances;
        int shadowScale;
        int nearLocation;
        int depthScale;
        int depthTexCoordOffset;
        int depthTexCoordScale;
        int radius;
        int ambientSphere;
    };
    
    static void loadLightProgram(const char* fragSource, bool limited, ProgramObject& program, LightLocations& locations);
   
    ProgramObject _simpleProgram;
    int _glowIntensityLocation;
    
    ProgramObject _directionalAmbientSphereLight;
    LightLocations _directionalAmbientSphereLightLocations;
    ProgramObject _directionalAmbientSphereLightShadowMap;
    LightLocations _directionalAmbientSphereLightShadowMapLocations;
    ProgramObject _directionalAmbientSphereLightCascadedShadowMap;
    LightLocations _directionalAmbientSphereLightCascadedShadowMapLocations;

    ProgramObject _directionalLight;
    LightLocations _directionalLightLocations;
    ProgramObject _directionalLightShadowMap;
    LightLocations _directionalLightShadowMapLocations;
    ProgramObject _directionalLightCascadedShadowMap;
    LightLocations _directionalLightCascadedShadowMapLocations;

    ProgramObject _pointLight;
    LightLocations _pointLightLocations;
    ProgramObject _spotLight;
    LightLocations _spotLightLocations;
    
    class PointLight {
    public:
        glm::vec4 position;
        float radius;
        glm::vec4 ambient;
        glm::vec4 diffuse;
        glm::vec4 specular;
        float constantAttenuation;
        float linearAttenuation;
        float quadraticAttenuation;
    };
    
    class SpotLight : public PointLight {
    public:
        glm::vec3 direction;
        float exponent;
        float cutoff;
    };
    
    QVector<PointLight> _pointLights;
    QVector<SpotLight> _spotLights;
    QVector<PostLightingRenderable*> _postLightingRenderables;
    
    AbstractViewStateInterface* _viewState;

    int _ambientLightMode = 0;
};

/// Simple interface for objects that require something to be rendered after deferred lighting.
class PostLightingRenderable {
public:
    virtual void renderPostLighting() = 0;
};

#endif // hifi_DeferredLightingEffect_h
