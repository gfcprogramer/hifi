//
//  PlaneShape.h
//  libraries/shared/src
//
//  Created by Andrzej Kapolka on 4/9/2014.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PlaneShape_h
#define hifi_PlaneShape_h

#include "Shape.h"

class PlaneShape : public Shape {
public:
    PlaneShape(const glm::vec4& coefficients = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    
    glm::vec3 getNormal() const;
    glm::vec4 getCoefficients() const;

    void setNormal(const glm::vec3& normal);
    void setPoint(const glm::vec3& point);

    bool findRayIntersection(RayIntersectionInfo& intersection) const;
};

#endif // hifi_PlaneShape_h
