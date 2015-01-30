//
//  EnvironmentData.h
//  libraries/environment/src
//
//  Created by Andrzej Kapolka on 5/6/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EnvironmentData_h
#define hifi_EnvironmentData_h

#include <glm/glm.hpp>

class EnvironmentData {
public:

    EnvironmentData(int id = 0);

    void setID(int id) { _id = id; }
    int getID() const { return _id; }

    void setFlat(bool flat) { _flat = flat; }
    bool isFlat() const { return _flat; }

    void setGravity(float gravity) { _gravity = gravity; }
    float getGravity() const { return _gravity; }

    void setAtmosphereCenter(const glm::vec3& center) { _atmosphereCenter = center; }
    void setAtmosphereInnerRadius(float radius) { _atmosphereInnerRadius = radius; }
    void setAtmosphereOuterRadius(float radius) { _atmosphereOuterRadius = radius; }
    const glm::vec3& getAtmosphereCenter() const { return _atmosphereCenter; }
    float getAtmosphereInnerRadius() const { return _atmosphereInnerRadius; }
    float getAtmosphereOuterRadius() const { return _atmosphereOuterRadius; }
    
    void setRayleighScattering(float scattering) { _rayleighScattering = scattering; }
    void setMieScattering(float scattering) { _mieScattering = scattering; }
    float getRayleighScattering() const { return _rayleighScattering; }
    float getMieScattering() const { return _mieScattering; }
    
    void setScatteringWavelengths(const glm::vec3& wavelengths) { _scatteringWavelengths = wavelengths; }
    const glm::vec3& getScatteringWavelengths() const { return _scatteringWavelengths; }
    
    void setSunLocation(const glm::vec3& location) { _sunLocation = location; }
    void setSunBrightness(float brightness) { _sunBrightness = brightness; }
    const glm::vec3& getSunLocation() const { return _sunLocation; }
    float getSunBrightness() const { return _sunBrightness; }

    glm::vec3 getAtmosphereCenter(const glm::vec3& cameraPosition) const;
    glm::vec3 getSunLocation(const glm::vec3& cameraPosition) const;

    int getBroadcastData(unsigned char* destinationBuffer) const;
    int parseData(const unsigned char* sourceBuffer, int numBytes);
    
private:

    int _id;
    
    bool _flat;
    
    float _gravity;

    glm::vec3 _atmosphereCenter;
    float _atmosphereInnerRadius;
    float _atmosphereOuterRadius;
    
    float _rayleighScattering;
    float _mieScattering;
    
    glm::vec3 _scatteringWavelengths;
    
    glm::vec3 _sunLocation;
    float _sunBrightness;
};

#endif // hifi_EnvironmentData_h
