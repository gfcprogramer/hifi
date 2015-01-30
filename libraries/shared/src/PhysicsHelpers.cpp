//
//  PhysicsHelpers.cpp
//  libraries/shared/src
//
//  Created by Andrew Meadows 2015.01.27
//  Unless otherwise copyrighted: Copyright 2015 High Fidelity, Inc.
//
//  Unless otherwise licensced: Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "PhysicsHelpers.h"
#include "SharedUtil.h"

// This chunk of code was copied from Bullet-2.82, so we include the Bullet license here:
/*
 * Bullet Continuous Collision Detection and Physics Library
 * Copyright (c) 2003-2009 Erwin Coumans  http://bulletphysics.org
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from the use of this software.
 * Permission is granted to anyone to use this software for any purpose, 
 * including commercial applications, and to alter it and redistribute it freely, 
 * subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. 
 *    If you use this software in a product, an acknowledgment in the product documentation would be appreciated but 
 *    is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Copied and modified from LinearMath/btTransformUtil.h by AndrewMeadows on 2015.01.27.
 * */
glm::quat computeBulletRotationStep(const glm::vec3& angularVelocity, float timeStep) {
    // Bullet uses an exponential map approximation technique to integrate rotation.
    // The reason for this is to make it easy to compute the derivative of angular motion for various consraints.
    // Here we reproduce the same approximation so that our extrapolations agree well with Bullet's integration.
    //
    // Exponential map
    // google for "Practical Parameterization of Rotations Using the Exponential Map", F. Sebastian Grassia

    float speed = glm::length(angularVelocity);
    // limit the angular motion because the exponential approximation fails for large steps
    const float ANGULAR_MOTION_THRESHOLD = 0.5f * PI_OVER_TWO;
    if (speed * timeStep > ANGULAR_MOTION_THRESHOLD) {
        speed = ANGULAR_MOTION_THRESHOLD / timeStep;
    }

    glm::vec3 axis = angularVelocity;
    if (speed < 0.001f) {
        // use Taylor's expansions of sync function
        axis *= (0.5f * timeStep - (timeStep * timeStep * timeStep) * (0.020833333333f * speed * speed));
    } else {
        // sync(speed) = sin(c * speed)/t
        axis *= (sinf(0.5f * speed * timeStep) / speed );
    }
    return glm::quat(cosf(0.5f * speed * timeStep), axis.x, axis.y, axis.z);
}
