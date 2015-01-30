//
//  ShapeCollider.cpp
//  libraries/shared/src
//
//  Created by Andrew Meadows on 02/20/2014.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <glm/gtx/norm.hpp>

#include "AACubeShape.h"
#include "CapsuleShape.h"
#include "GeometryUtil.h"
#include "ListShape.h"
#include "PlaneShape.h"
#include "ShapeCollider.h"
#include "SphereShape.h"
#include "StreamUtils.h"


// NOTE:
//
// * Large ListShape's are inefficient keep the lists short.
// * Collisions between lists of lists work in theory but are not recommended.

const quint8 NUM_SHAPE_TYPES = UNKNOWN_SHAPE;
const quint8 NUM_DISPATCH_CELLS = NUM_SHAPE_TYPES * NUM_SHAPE_TYPES;

Shape::Type getDispatchKey(Shape::Type typeA, Shape::Type typeB) {
    return typeA + NUM_SHAPE_TYPES * typeB;
}

// dummy dispatch for any non-implemented pairings
bool notImplemented(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) { 
    return false; 
}

// NOTE: hardcode the number of dispatchTable entries (NUM_SHAPE_TYPES ^2)
bool (*dispatchTable[NUM_DISPATCH_CELLS])(const Shape*, const Shape*, CollisionList&);

namespace ShapeCollider {

// NOTE: the dispatch table must be initialized before the ShapeCollider is used.
void initDispatchTable() {
    for (Shape::Type i = 0; i < NUM_DISPATCH_CELLS; ++i) {
        dispatchTable[i] = &notImplemented;
    }

    dispatchTable[getDispatchKey(SPHERE_SHAPE, SPHERE_SHAPE)] = &sphereVsSphere;
    dispatchTable[getDispatchKey(SPHERE_SHAPE, CAPSULE_SHAPE)] = &sphereVsCapsule;
    dispatchTable[getDispatchKey(SPHERE_SHAPE, PLANE_SHAPE)] = &sphereVsPlane;
    dispatchTable[getDispatchKey(SPHERE_SHAPE, AACUBE_SHAPE)] = &sphereVsAACube;
    dispatchTable[getDispatchKey(SPHERE_SHAPE, LIST_SHAPE)] = &shapeVsList;

    dispatchTable[getDispatchKey(CAPSULE_SHAPE, SPHERE_SHAPE)] = &capsuleVsSphere;
    dispatchTable[getDispatchKey(CAPSULE_SHAPE, CAPSULE_SHAPE)] = &capsuleVsCapsule;
    dispatchTable[getDispatchKey(CAPSULE_SHAPE, PLANE_SHAPE)] = &capsuleVsPlane;
    dispatchTable[getDispatchKey(CAPSULE_SHAPE, AACUBE_SHAPE)] = &capsuleVsAACube;
    dispatchTable[getDispatchKey(CAPSULE_SHAPE, LIST_SHAPE)] = &shapeVsList;

    dispatchTable[getDispatchKey(PLANE_SHAPE, SPHERE_SHAPE)] = &planeVsSphere;
    dispatchTable[getDispatchKey(PLANE_SHAPE, CAPSULE_SHAPE)] = &planeVsCapsule;
    dispatchTable[getDispatchKey(PLANE_SHAPE, PLANE_SHAPE)] = &planeVsPlane;
    dispatchTable[getDispatchKey(PLANE_SHAPE, AACUBE_SHAPE)] = &notImplemented;
    dispatchTable[getDispatchKey(PLANE_SHAPE, LIST_SHAPE)] = &shapeVsList;

    dispatchTable[getDispatchKey(AACUBE_SHAPE, SPHERE_SHAPE)] = &aaCubeVsSphere;
    dispatchTable[getDispatchKey(AACUBE_SHAPE, CAPSULE_SHAPE)] = &aaCubeVsCapsule;
    dispatchTable[getDispatchKey(AACUBE_SHAPE, PLANE_SHAPE)] = &notImplemented;
    dispatchTable[getDispatchKey(AACUBE_SHAPE, AACUBE_SHAPE)] = &aaCubeVsAACube;
    dispatchTable[getDispatchKey(AACUBE_SHAPE, LIST_SHAPE)] = &shapeVsList;

    dispatchTable[getDispatchKey(LIST_SHAPE, SPHERE_SHAPE)] = &listVsShape;
    dispatchTable[getDispatchKey(LIST_SHAPE, CAPSULE_SHAPE)] = &listVsShape;
    dispatchTable[getDispatchKey(LIST_SHAPE, PLANE_SHAPE)] = &listVsShape;
    dispatchTable[getDispatchKey(LIST_SHAPE, AACUBE_SHAPE)] = &listVsShape;
    dispatchTable[getDispatchKey(LIST_SHAPE, LIST_SHAPE)] = &listVsList;

    // all of the UNKNOWN_SHAPE pairings are notImplemented
}

bool collideShapes(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    return (*dispatchTable[shapeA->getType() + NUM_SHAPE_TYPES * shapeB->getType()])(shapeA, shapeB, collisions);
}

static CollisionList tempCollisions(32);

bool collideShapeWithShapes(const Shape* shapeA, const QVector<Shape*>& shapes, int startIndex, CollisionList& collisions) {
    bool collided = false;
    if (shapeA) {
        int numShapes = shapes.size();
        for (int i = startIndex; i < numShapes; ++i) {
            const Shape* shapeB = shapes.at(i);
            if (!shapeB) {
                continue;
            }
            if (collideShapes(shapeA, shapeB, collisions)) {
                collided = true;
                if (collisions.isFull()) {
                    break;
                }
            }
        }
    }
    return collided;
}

bool collideShapesWithShapes(const QVector<Shape*>& shapesA, const QVector<Shape*>& shapesB, CollisionList& collisions) {
    bool collided = false;
    int numShapesA = shapesA.size();
    for (int i = 0; i < numShapesA; ++i) {
        Shape* shapeA = shapesA.at(i);
        if (!shapeA) {
            continue;
        }
        if (collideShapeWithShapes(shapeA, shapesB, 0, collisions)) {
            collided = true;
            if (collisions.isFull()) {
                break;
            }
        }
    }
    return collided;
}

bool collideShapeWithAACubeLegacy(const Shape* shapeA, const glm::vec3& cubeCenter, float cubeSide, CollisionList& collisions) {
    Shape::Type typeA = shapeA->getType();
    if (typeA == SPHERE_SHAPE) {
        return sphereVsAACubeLegacy(static_cast<const SphereShape*>(shapeA), cubeCenter, cubeSide, collisions);
    } else if (typeA == CAPSULE_SHAPE) {
        return capsuleVsAACubeLegacy(static_cast<const CapsuleShape*>(shapeA), cubeCenter, cubeSide, collisions);
    } else if (typeA == LIST_SHAPE) {
        const ListShape* listA = static_cast<const ListShape*>(shapeA);
        bool touching = false;
        for (int i = 0; i < listA->size() && !collisions.isFull(); ++i) {
            const Shape* subShape = listA->getSubShape(i);
            int subType = subShape->getType();
            if (subType == SPHERE_SHAPE) {
                touching = sphereVsAACubeLegacy(static_cast<const SphereShape*>(subShape), cubeCenter, cubeSide, collisions) || touching;
            } else if (subType == CAPSULE_SHAPE) {
                touching = capsuleVsAACubeLegacy(static_cast<const CapsuleShape*>(subShape), cubeCenter, cubeSide, collisions) || touching;
            }
        }
        return touching;
    }
    return false;
}

bool sphereVsSphere(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    const SphereShape* sphereA = static_cast<const SphereShape*>(shapeA);
    const SphereShape* sphereB = static_cast<const SphereShape*>(shapeB);
    glm::vec3 BA = sphereB->getTranslation() - sphereA->getTranslation();
    float distanceSquared = glm::dot(BA, BA);
    float totalRadius = sphereA->getRadius() + sphereB->getRadius();
    if (distanceSquared < totalRadius * totalRadius) {
        // normalize BA
        float distance = sqrtf(distanceSquared);
        if (distance < EPSILON) {
            // the spheres are on top of each other, so we pick an arbitrary penetration direction
            BA = glm::vec3(0.0f, 1.0f, 0.0f);
            distance = totalRadius;
        } else {
            BA /= distance;
        }
        // penetration points from A into B
        CollisionInfo* collision = collisions.getNewCollision();
        if (collision) {
            collision->_penetration = BA * (totalRadius - distance);
            // contactPoint is on surface of A
            collision->_contactPoint = sphereA->getTranslation() + sphereA->getRadius() * BA;
            collision->_shapeA = shapeA;
            collision->_shapeB = shapeB;
            return true;
        }
    }
    return false;
}

bool sphereVsCapsule(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    const SphereShape* sphereA = static_cast<const SphereShape*>(shapeA);
    const CapsuleShape* capsuleB = static_cast<const CapsuleShape*>(shapeB);
    // find sphereA's closest approach to axis of capsuleB
    glm::vec3 BA = capsuleB->getTranslation() - sphereA->getTranslation();
    glm::vec3 capsuleAxis; 
    capsuleB->computeNormalizedAxis(capsuleAxis);
    float axialDistance = - glm::dot(BA, capsuleAxis);
    float absAxialDistance = fabsf(axialDistance);
    float totalRadius = sphereA->getRadius() + capsuleB->getRadius();
    if (absAxialDistance < totalRadius + capsuleB->getHalfHeight()) {
        glm::vec3 radialAxis = BA + axialDistance * capsuleAxis; // points from A to axis of B
        float radialDistance2 = glm::length2(radialAxis);
        float totalRadius2 = totalRadius * totalRadius;
        if (radialDistance2 > totalRadius2) {
            // sphere is too far from capsule axis
            return false;
        }
        if (absAxialDistance > capsuleB->getHalfHeight()) {
            // sphere hits capsule on a cap --> recompute radialAxis to point from spherA to cap center
            float sign = (axialDistance > 0.0f) ? 1.0f : -1.0f;
            radialAxis = BA + (sign * capsuleB->getHalfHeight()) * capsuleAxis;
            radialDistance2 = glm::length2(radialAxis);
            if (radialDistance2 > totalRadius2) {
                return false;
            }
        }
        if (radialDistance2 > EPSILON * EPSILON) {
            CollisionInfo* collision = collisions.getNewCollision();
            if (!collision) {
                // collisions list is full
                return false;
            }
            // normalize the radialAxis
            float radialDistance = sqrtf(radialDistance2);
            radialAxis /= radialDistance;
            // penetration points from A into B
            collision->_penetration = (totalRadius - radialDistance) * radialAxis; // points from A into B
            // contactPoint is on surface of sphereA
            collision->_contactPoint = sphereA->getTranslation() + sphereA->getRadius() * radialAxis;
            collision->_shapeA = shapeA;
            collision->_shapeB = shapeB;
        } else {
            // A is on B's axis, so the penetration is undefined... 
            if (absAxialDistance > capsuleB->getHalfHeight()) {
                // ...for the cylinder case (for now we pretend the collision doesn't exist)
                return false;
            }
            CollisionInfo* collision = collisions.getNewCollision();
            if (!collision) {
                // collisions list is full
                return false;
            }
            // ... but still defined for the cap case
            if (axialDistance < 0.0f) {
                // we're hitting the start cap, so we negate the capsuleAxis
                capsuleAxis *= -1;
            }
            // penetration points from A into B
            float sign = (axialDistance > 0.0f) ? -1.0f : 1.0f;
            collision->_penetration = (sign * (totalRadius + capsuleB->getHalfHeight() - absAxialDistance)) * capsuleAxis;
            // contactPoint is on surface of sphereA
            collision->_contactPoint = sphereA->getTranslation() + (sign * sphereA->getRadius()) * capsuleAxis;
            collision->_shapeA = shapeA;
            collision->_shapeB = shapeB;
        }
        return true;
    }
    return false;
}

bool sphereVsPlane(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    const SphereShape* sphereA = static_cast<const SphereShape*>(shapeA);
    const PlaneShape* planeB = static_cast<const PlaneShape*>(shapeB);
    glm::vec3 penetration;
    if (findSpherePlanePenetration(sphereA->getTranslation(), sphereA->getRadius(), planeB->getCoefficients(), penetration)) {
        CollisionInfo* collision = collisions.getNewCollision();
        if (!collision) {
            return false; // collision list is full
        }
        collision->_penetration = penetration;
        collision->_contactPoint = sphereA->getTranslation() + sphereA->getRadius() * glm::normalize(penetration);
        collision->_shapeA = shapeA;
        collision->_shapeB = shapeB;
        return true;
    }
    return false;
}

bool capsuleVsSphere(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    return sphereVsCapsule(shapeB, shapeA, collisions);
}

/// \param lineP point on line
/// \param lineDir normalized direction of line
/// \param cylinderP point on cylinder axis
/// \param cylinderDir normalized direction of cylinder axis
/// \param cylinderRadius radius of cylinder
/// \param hitLow[out] distance from point on line to first intersection with cylinder
/// \param hitHigh[out] distance from point on line to second intersection with cylinder
/// \return true if line hits cylinder
bool lineCylinder(const glm::vec3& lineP, const glm::vec3& lineDir, 
        const glm::vec3& cylinderP, const glm::vec3& cylinderDir, float cylinderRadius,
        float& hitLow, float& hitHigh) {

    // first handle parallel case
    float uDotV = glm::dot(lineDir, cylinderDir);
    if (fabsf(1.0f - fabsf(uDotV)) < EPSILON) {
        // line and cylinder are parallel
        if (glm::distance2(lineP, cylinderP) <= cylinderRadius * cylinderRadius) {
            // line is inside cylinder, which we consider a hit
            hitLow = 0.0f;
            hitHigh = 0.0f;
            return true;
        }
        return false;
    }
    
    // Given a line with point 'p' and normalized direction 'u' and 
    // a cylinder with axial point 's', radius 'r', and normalized direction 'v'
    // the intersection of the two is on the line at distance 't' from 'p'.
    //
    // Determining the values of t reduces to solving a quadratic equation: At^2 + Bt + C = 0 
    //
    // where:
    //
    // P = p-s
    // w = u-(u.v)v
    // Q = P-(P.v)v
    //       
    // A = w^2
    // B = 2(w.Q)
    // C = Q^2 - r^2

    glm::vec3 P = lineP - cylinderP;
    glm::vec3 w = lineDir - uDotV * cylinderDir;
    glm::vec3 Q = P - glm::dot(P, cylinderDir) * cylinderDir;

    // we save a few multiplies by storing 2*A rather than just A
    float A2 = 2.0f * glm::dot(w, w);
    float B = 2.0f * glm::dot(w, Q);

    // since C is only ever used once (in the determinant) we compute it inline
    float determinant = B * B - 2.0f * A2 * (glm::dot(Q, Q) - cylinderRadius * cylinderRadius);
    if (determinant < 0.0f) {
        return false;
    }
    hitLow  = (-B - sqrtf(determinant)) / A2;
    hitHigh = -(hitLow + 2.0f * B / A2);

    if (hitLow > hitHigh) {
        // re-arrange so hitLow is always the smaller value
        float temp = hitHigh;
        hitHigh = hitLow;
        hitLow = temp;
    }
    return true;
}

bool capsuleVsCapsule(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    const CapsuleShape* capsuleA = static_cast<const CapsuleShape*>(shapeA);
    const CapsuleShape* capsuleB = static_cast<const CapsuleShape*>(shapeB);
    glm::vec3 axisA;
    capsuleA->computeNormalizedAxis(axisA);
    glm::vec3 axisB;
    capsuleB->computeNormalizedAxis(axisB);
    glm::vec3 centerA = capsuleA->getTranslation();
    glm::vec3 centerB = capsuleB->getTranslation();

    // NOTE: The formula for closest approach between two lines is:
    // d = [(B - A) . (a - (a.b)b)] / (1 - (a.b)^2)

    float aDotB = glm::dot(axisA, axisB);
    float denominator = 1.0f - aDotB * aDotB;
    float totalRadius = capsuleA->getRadius() + capsuleB->getRadius();
    if (denominator > EPSILON) {
        // perform line-cylinder intesection test between axis of cylinderA and cylinderB with exanded radius
        float hitLow = 0.0f;
        float hitHigh = 0.0f;
        if (!lineCylinder(centerA, axisA, centerB, axisB, totalRadius, hitLow, hitHigh)) {
            return false;
        }

        float halfHeightA = capsuleA->getHalfHeight();
        if (hitLow > halfHeightA || hitHigh < -halfHeightA) {
            // the intersections are off the ends of capsuleA
            return false;
        }

        // compute nearest approach on axisA of axisB
        float distanceA = glm::dot((centerB - centerA), (axisA - (aDotB) * axisB)) / denominator;
        // clamp to intersection zone
        if (distanceA > hitLow) {
            if (distanceA > hitHigh) {
                distanceA = hitHigh;
            }
        } else {
            distanceA = hitLow;
        }
        // clamp to capsule segment
        distanceA = glm::clamp(distanceA, -halfHeightA, halfHeightA);

        // find the closest point on capsuleB to sphere on capsuleA
        float distanceB = glm::dot(centerA + distanceA * axisA - centerB, axisB);
        float halfHeightB = capsuleB->getHalfHeight();
        if (fabsf(distanceB) > halfHeightB) {
            // we must clamp distanceB...
            distanceB = glm::clamp(distanceB, -halfHeightB, halfHeightB);
            // ...and therefore must recompute distanceA
            distanceA = glm::clamp(glm::dot(centerB + distanceB * axisB - centerA, axisA), -halfHeightA, halfHeightA);
        }

        // collide like two spheres (do most of the math relative to B)
        glm::vec3 BA = (centerB + distanceB * axisB) - (centerA + distanceA * axisA);
        float distanceSquared = glm::dot(BA, BA);
        if (distanceSquared < totalRadius * totalRadius) {
            CollisionInfo* collision = collisions.getNewCollision();
            if (!collision) {
                // collisions list is full
                return false;
            }
            // normalize BA
            float distance = sqrtf(distanceSquared);
            if (distance < EPSILON) {
                // the contact spheres are on top of each other, so we need to pick a penetration direction...
                // try vector between the capsule centers...
                BA = centerB - centerA;
                distanceSquared = glm::length2(BA);
                if (distanceSquared > EPSILON * EPSILON) {
                    distance = sqrtf(distanceSquared);
                    BA /= distance;
                } else
                {
                    // the capsule centers are on top of each other!
                    // give up on a valid penetration direction and just use the yAxis
                    BA = glm::vec3(0.0f, 1.0f, 0.0f);
                    distance = glm::max(capsuleB->getRadius(), capsuleA->getRadius());
                }
            } else {
                BA /= distance;
            }
            // penetration points from A into B
            collision->_penetration = BA * (totalRadius - distance);
            // contactPoint is on surface of A
            collision->_contactPoint = centerA + distanceA * axisA + capsuleA->getRadius() * BA;
            collision->_shapeA = shapeA;
            collision->_shapeB = shapeB;
            return true;
        }
    } else {
        // capsules are approximiately parallel but might still collide
        glm::vec3 BA = centerB - centerA;
        float axialDistance = glm::dot(BA, axisB);
        if (fabsf(axialDistance) > totalRadius + capsuleA->getHalfHeight() + capsuleB->getHalfHeight()) {
            return false;
        }
        BA = BA - axialDistance * axisB;     // BA now points from centerA to axisB (perp to axis)
        float distanceSquared = glm::length2(BA);
        if (distanceSquared < totalRadius * totalRadius) {
            CollisionInfo* collision = collisions.getNewCollision();
            if (!collision) {
                // collisions list is full
                return false;
            }
            // We have all the info we need to compute the penetration vector...
            // normalize BA
            float distance = sqrtf(distanceSquared);
            if (distance < EPSILON) {
                // the spheres are on top of each other, so we pick an arbitrary penetration direction
                BA = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                BA /= distance;
            }
            // penetration points from A into B
            collision->_penetration = BA * (totalRadius - distance);

            // However we need some more world-frame info to compute the contactPoint, 
            // which is on the surface of capsuleA...
            //
            // Find the overlapping secion of the capsules --> they collide as if there were
            // two spheres at the midpoint of this overlapping section.  
            // So we project all endpoints to axisB, find the interior pair, 
            // and put A's proxy sphere on axisA at the midpoint of this section.

            // sort the projections as much as possible during calculation
            float points[5];
            points[0] = -capsuleB->getHalfHeight();
            points[1] = axialDistance - capsuleA->getHalfHeight();
            points[2] = axialDistance + capsuleA->getHalfHeight();
            points[3] = capsuleB->getHalfHeight();

            // Since there are only three comparisons to do we unroll the sort algorithm...
            // and use a fifth slot as temp during swap.
            if (points[1] > points[2]) {
                points[4] = points[1];
                points[1] = points[2];
                points[2] = points[4];
            }
            if (points[2] > points[3]) {
                points[4] = points[2];
                points[2] = points[3];
                points[3] = points[4];
            }
            if (points[0] > points[1]) {
                points[4] = points[0];
                points[0] = points[1];
                points[1] = points[4];
            }

            // average the internal pair, and then do the math from centerB
            collision->_contactPoint = centerB + (0.5f * (points[1] + points[2])) * axisB 
                + (capsuleA->getRadius() - distance) * BA;
            collision->_shapeA = shapeA;
            collision->_shapeB = shapeB;
            return true;
        }
    }
    return false;
}

bool capsuleVsPlane(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    const CapsuleShape* capsuleA = static_cast<const CapsuleShape*>(shapeA);
    const PlaneShape* planeB = static_cast<const PlaneShape*>(shapeB);
    glm::vec3 start, end, penetration;
    capsuleA->getStartPoint(start);
    capsuleA->getEndPoint(end);
    glm::vec4 plane = planeB->getCoefficients();
    if (findCapsulePlanePenetration(start, end, capsuleA->getRadius(), plane, penetration)) {
        CollisionInfo* collision = collisions.getNewCollision();
        if (!collision) {
            return false; // collision list is full
        }
        collision->_penetration = penetration;
        glm::vec3 deepestEnd = (glm::dot(start, glm::vec3(plane)) < glm::dot(end, glm::vec3(plane))) ? start : end;
        collision->_contactPoint = deepestEnd + capsuleA->getRadius() * glm::normalize(penetration);
        collision->_shapeA = shapeA;
        collision->_shapeB = shapeB;
        return true;
    }
    return false;
}

bool planeVsSphere(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    return sphereVsPlane(shapeB, shapeA, collisions);
}

bool planeVsCapsule(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    return capsuleVsPlane(shapeB, shapeA, collisions);
}

bool planeVsPlane(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    // technically, planes always collide unless they're parallel and not coincident; however, that's
    // not going to give us any useful information
    return false;
}

// helper function
bool sphereVsAACubeLegacy(const glm::vec3& sphereCenter, float sphereRadius, const glm::vec3& cubeCenter, 
        float cubeSide, CollisionList& collisions) {
    // sphere is A
    // cube is B
    // BA = B - A = from center of A to center of B 
    float halfCubeSide = 0.5f * cubeSide;
    glm::vec3 BA = cubeCenter - sphereCenter;
    float distance = glm::length(BA);
    if (distance > EPSILON) {
        float maxBA = glm::max(glm::max(glm::abs(BA.x), glm::abs(BA.y)), glm::abs(BA.z));
        if (maxBA > halfCubeSide + sphereRadius) {
            // sphere misses cube entirely
            return false;
        } 
        CollisionInfo* collision = collisions.getNewCollision();
        if (!collision) {
            return false;
        }
        if (maxBA > halfCubeSide) {
            // sphere hits cube but its center is outside cube
            
            // compute contact anti-pole on cube (in cube frame)
            glm::vec3 cubeContact = glm::abs(BA);
            if (cubeContact.x > halfCubeSide) {
                cubeContact.x = halfCubeSide;
            }
            if (cubeContact.y > halfCubeSide) {
                cubeContact.y = halfCubeSide;
            }
            if (cubeContact.z > halfCubeSide) {
                cubeContact.z = halfCubeSide;
            }
            glm::vec3 signs = glm::sign(BA);
            cubeContact.x *= signs.x;
            cubeContact.y *= signs.y;
            cubeContact.z *= signs.z;

            // compute penetration direction
            glm::vec3 direction = BA - cubeContact;
            float lengthDirection = glm::length(direction);
            if (lengthDirection < EPSILON) {
                // sphereCenter is touching cube surface, so we can't use the difference between those two 
                // points to compute the penetration direction.  Instead we use the unitary components of 
                // cubeContact.
                direction = cubeContact / halfCubeSide;
                glm::modf(BA, direction);
                lengthDirection = glm::length(direction);
            } else if (lengthDirection > sphereRadius) {
                collisions.deleteLastCollision();
                return false;
            }
            direction /= lengthDirection;

            // compute collision details
            collision->_contactPoint = sphereCenter + sphereRadius * direction;
            collision->_penetration = sphereRadius * direction - (BA - cubeContact);
        } else {
            // sphere center is inside cube
            // --> push out nearest face
            glm::vec3 direction;
            BA /= maxBA;
            glm::modf(BA, direction);
            float lengthDirection = glm::length(direction);
            direction /= lengthDirection;

            // compute collision details
            collision->_floatData = cubeSide;
            collision->_vecData = cubeCenter;
            collision->_penetration = (halfCubeSide * lengthDirection + sphereRadius - maxBA * glm::dot(BA, direction)) * direction;
            collision->_contactPoint = sphereCenter + sphereRadius * direction;
        }
        collision->_floatData = cubeSide;
        collision->_vecData = cubeCenter;
        collision->_shapeA = NULL;
        collision->_shapeB = NULL;
        return true;
    } else if (sphereRadius + halfCubeSide > distance) {
        // NOTE: for cocentric approximation we collide sphere and cube as two spheres which means 
        // this algorithm will probably be wrong when both sphere and cube are very small (both ~EPSILON)
        CollisionInfo* collision = collisions.getNewCollision();
        if (collision) {
            // the penetration and contactPoint are undefined, so we pick a penetration direction (-yAxis)
            collision->_penetration = (sphereRadius + halfCubeSide) * glm::vec3(0.0f, -1.0f, 0.0f);
            // contactPoint is on surface of A
            collision->_contactPoint = sphereCenter + collision->_penetration;

            collision->_floatData = cubeSide;
            collision->_vecData = cubeCenter;
            collision->_shapeA = NULL;
            collision->_shapeB = NULL;
            return true;
        }
    }
    return false;
}

// helper function
CollisionInfo* sphereVsAACubeHelper(const glm::vec3& sphereCenter, float sphereRadius, const glm::vec3& cubeCenter, 
        float cubeSide, CollisionList& collisions) {
    // sphere is A
    // cube is B
    // BA = B - A = from center of A to center of B 
    float halfCubeSide = 0.5f * cubeSide;
    glm::vec3 BA = cubeCenter - sphereCenter;
    float distance = glm::length(BA);
    if (distance > EPSILON) {
        float maxBA = glm::max(glm::max(glm::abs(BA.x), glm::abs(BA.y)), glm::abs(BA.z));
        if (maxBA > halfCubeSide + sphereRadius) {
            // sphere misses cube entirely
            return NULL;
        } 
        CollisionInfo* collision = collisions.getNewCollision();
        if (!collision) {
            return NULL;
        }
        if (maxBA > halfCubeSide) {
            // sphere hits cube but its center is outside cube
            
            // compute contact anti-pole on cube (in cube frame)
            glm::vec3 cubeContact = glm::abs(BA);
            if (cubeContact.x > halfCubeSide) {
                cubeContact.x = halfCubeSide;
            }
            if (cubeContact.y > halfCubeSide) {
                cubeContact.y = halfCubeSide;
            }
            if (cubeContact.z > halfCubeSide) {
                cubeContact.z = halfCubeSide;
            }
            glm::vec3 signs = glm::sign(BA);
            cubeContact.x *= signs.x;
            cubeContact.y *= signs.y;
            cubeContact.z *= signs.z;

            // compute penetration direction
            glm::vec3 direction = BA - cubeContact;
            float lengthDirection = glm::length(direction);
            if (lengthDirection < EPSILON) {
                // sphereCenter is touching cube surface, so we can't use the difference between those two 
                // points to compute the penetration direction.  Instead we use the unitary components of 
                // cubeContact.
                glm::modf(cubeContact / halfCubeSide, direction);
                lengthDirection = glm::length(direction);
            } else if (lengthDirection > sphereRadius) {
                collisions.deleteLastCollision();
                return NULL;
            }
            direction /= lengthDirection;

            // compute collision details
            collision->_contactPoint = sphereCenter + sphereRadius * direction;
            collision->_penetration = sphereRadius * direction - (BA - cubeContact);
        } else {
            // sphere center is inside cube
            // --> push out nearest face
            glm::vec3 direction;
            BA /= maxBA;
            glm::modf(BA, direction);
            float lengthDirection = glm::length(direction);
            direction /= lengthDirection;

            // compute collision details
            collision->_floatData = cubeSide;
            collision->_vecData = cubeCenter;
            collision->_penetration = (halfCubeSide * lengthDirection + sphereRadius - maxBA * glm::dot(BA, direction)) * direction;
            collision->_contactPoint = sphereCenter + sphereRadius * direction;
        }
        collision->_shapeA = NULL;
        collision->_shapeB = NULL;
        return collision;
    } else if (sphereRadius + halfCubeSide > distance) {
        // NOTE: for cocentric approximation we collide sphere and cube as two spheres which means 
        // this algorithm will probably be wrong when both sphere and cube are very small (both ~EPSILON)
        CollisionInfo* collision = collisions.getNewCollision();
        if (collision) {
            // the penetration and contactPoint are undefined, so we pick a penetration direction (-yAxis)
            collision->_penetration = (sphereRadius + halfCubeSide) * glm::vec3(0.0f, -1.0f, 0.0f);
            // contactPoint is on surface of A
            collision->_contactPoint = sphereCenter + collision->_penetration;
            collision->_shapeA = NULL;
            collision->_shapeB = NULL;
            return collision;
        }
    }
    return NULL;
}

bool sphereVsAACube(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    // BA = B - A = from center of A to center of B 
    const SphereShape* sphereA = static_cast<const SphereShape*>(shapeA);
    const AACubeShape* cubeB = static_cast<const AACubeShape*>(shapeB);

    CollisionInfo* collision = sphereVsAACubeHelper( sphereA->getTranslation(), sphereA->getRadius(),
            cubeB->getTranslation(), cubeB->getScale(), collisions);
    if (collision) {
        collision->_shapeA = shapeA;
        collision->_shapeB = shapeB;
        return true;
    }
    return false;
}

glm::vec3 cubeNormals[3] = { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) };

// the wallIndices is a sequence of pairs that represent the OTHER directions for each cube face, 
// hence the first pair is (1, 2) because the OTHER faces for xFace are (yFace, zFace) = (1, 2)
int wallIndices[] = { 1, 2, 0, 2, 0, 1 };

bool capsuleVsAACubeFace(const CapsuleShape* capsuleA, const AACubeShape* cubeB, int faceIndex, const glm::vec3& faceNormal, CollisionList& collisions) {
    // we only fall in here when the capsuleAxis is nearly parallel to the face of a cube
    glm::vec3 capsuleAxis; 
    capsuleA->computeNormalizedAxis(capsuleAxis);
    glm::vec3 cubeCenter = cubeB->getTranslation();

    // Revisualize the capsule as a line segment between two points.  We'd like to collide the 
    // capsule as two spheres located at the endpoints or where the line segment hits the boundary
    // of the face.

    // We raytrace forward into the four planes that neigbhor the face to find the boundary 
    // points of the line segment.
    glm::vec3 capsuleStart;
    capsuleA->getStartPoint(capsuleStart);

    // translate into cube-relative frame
    capsuleStart -= cubeCenter;
    float capsuleLength = 2.0f * capsuleA->getHalfHeight();
    float halfCubeSide = 0.5f * cubeB->getScale();
    float capsuleRadius = capsuleA->getRadius();

    // preload distances with values that work for when the capsuleAxis runs parallel to neighbor face
    float distances[] = {FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, 0.0f};

    // Loop over the directions that are NOT parallel to face (there are two of them).
    // For each direction we'll raytrace against the positive and negative planes to find where 
    // the axis passes through.
    for (int i = 0; i < 2; ++i) {
        int wallIndex = wallIndices[2 * faceIndex + i];
        glm::vec3 wallNormal = cubeNormals[wallIndex];
        // each direction has two walls: positive and negative
        float axisDotWall = glm::dot(capsuleAxis, wallNormal);
        if (fabsf(axisDotWall) > EPSILON) {
            // formula for distance to intersection between line (P,p) and plane (V,n) is: (V-P)*n / p*n
            distances[2 * i] = (halfCubeSide - glm::dot(capsuleStart, wallNormal)) / axisDotWall;
            distances[2 * i + 1] = -(halfCubeSide + glm::dot(capsuleStart, wallNormal)) / axisDotWall;
        }
    }

    // sort the distances from large to small
    int j = 3;
    while (j > 0) {
        for (int i = 0; i <= j; ++i) {
            if (distances[i] < distances[i+1]) {
                // swap (using distances[4] as temp space)
                distances[4] = distances[i];    
                distances[i] = distances[i+1];
                distances[i+1] = distances[4];
            } 
        }
        --j;
    }

    // the capsule overlaps when the max of the mins is less than the min of the maxes
    distances[0] = glm::min(capsuleLength, distances[1]); // maxDistance
    distances[1] = glm::max(0.0f, distances[2]); // minDistance
    bool hit = false;
    if (distances[1] < distances[0]) {
        // if we collide at all it will be at two points
        for (int i = 0; i < 2; ++i) {
            glm::vec3 sphereCenter = cubeCenter + capsuleStart + distances[i] * capsuleAxis;
            // collide like a sphere at point0 with capsuleRadius
            CollisionInfo* collision = sphereVsAACubeHelper(sphereCenter, capsuleRadius,
                    cubeCenter, 2.0f * halfCubeSide, collisions);
            if (collision) {
                // we hit! so store back pointers to the shapes
                collision->_shapeA = capsuleA;
                collision->_shapeB = cubeB;
                hit = true;
            }
        }
        return hit;
    } else if (distances[1] < capsuleLength + capsuleRadius ) {
        // we might collide at the end cap
        glm::vec3 sphereCenter = cubeCenter + capsuleStart + capsuleLength * capsuleAxis;
        // collide like a sphere at point0 with capsuleRadius
        CollisionInfo* collision = sphereVsAACubeHelper(sphereCenter, capsuleRadius,
                cubeCenter, 2.0f * halfCubeSide, collisions);
        if (collision) {
            // we hit! so store back pointers to the shapes
            collision->_shapeA = capsuleA;
            collision->_shapeB = cubeB;
            hit = true;
        }
    } else if (distances[0] > -capsuleLength) {
        // we might collide at the start cap
        glm::vec3 sphereCenter = cubeCenter + capsuleStart;
        // collide like a sphere at point0 with capsuleRadius
        CollisionInfo* collision = sphereVsAACubeHelper(sphereCenter, capsuleRadius,
                cubeCenter, 2.0f * halfCubeSide, collisions);
        if (collision) {
            // we hit! so store back pointers to the shapes
            collision->_shapeA = capsuleA;
            collision->_shapeB = cubeB;
            hit = true;
        }
    }
    return hit;
}

bool capsuleVsAACube(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    const CapsuleShape* capsuleA = static_cast<const CapsuleShape*>(shapeA);
    const AACubeShape* cubeB = static_cast<const AACubeShape*>(shapeB);

    // find nearest approach of capsule's line segment to cube's center
    glm::vec3 capsuleAxis; 
    capsuleA->computeNormalizedAxis(capsuleAxis);
    float halfHeight = capsuleA->getHalfHeight();
    glm::vec3 cubeCenter = cubeB->getTranslation();
    glm::vec3 capsuleCenter = capsuleA->getTranslation();
    glm::vec3 BA = cubeCenter - capsuleCenter;
    float axialOffset = glm::dot(capsuleAxis, BA);
    if (fabsf(axialOffset) > halfHeight) {
        axialOffset = (axialOffset < 0.0f) ? -halfHeight : halfHeight;
    }
    glm::vec3 nearestApproach = capsuleCenter + axialOffset * capsuleAxis;

    // transform nearestApproach into cube-relative frame
    nearestApproach -= cubeCenter;

    // determine the face of nearest approach
    glm::vec3 signs = glm::sign(nearestApproach);
    int faceIndex = 0;
    glm::vec3 faceNormal(signs.x, 0.0f, 0.0f);
    float maxApproach = fabsf(nearestApproach.x);
    if (maxApproach < fabsf(nearestApproach.y)) {
        maxApproach = fabsf(nearestApproach.y);
        faceIndex = 1;
        faceNormal = glm::vec3(0.0f, signs.y, 0.0f);
    }
    if (maxApproach < fabsf(nearestApproach.z)) {
        maxApproach = fabsf(nearestApproach.z);
        faceIndex = 2;
        faceNormal = glm::vec3(0.0f, 0.0f, signs.z);
    }

    if (fabs(glm::dot(faceNormal, capsuleAxis)) < EPSILON) {
        if (glm::dot(nearestApproach, faceNormal) > cubeB->getScale() + capsuleA->getRadius()) {
            return false;
        }
        // we expect this case to be rare but complicated enough that we split it out 
        // into its own helper function
        return capsuleVsAACubeFace(capsuleA, cubeB, faceIndex, faceNormal, collisions);
    }

    // Revisualize the capsule as a startPoint and an axis that points toward the cube face.
    // We raytrace forward into the four planes that neigbhor the face to find the furthest 
    // point along the capsule's axis that might hit face.
    glm::vec3 capsuleStart;
    if (glm::dot(capsuleAxis, faceNormal) < 0.0f) {
        capsuleA->getStartPoint(capsuleStart);
    } else {
        // NOTE: we want dot(capsuleAxis, faceNormal) to be negative which simplifies some
        // logic below, so we pretend the end is the start thereby reversing its axis.
        capsuleA->getEndPoint(capsuleStart);
        capsuleAxis *= -1.0f;
    }
    // translate into cube-relative frame
    capsuleStart -= cubeCenter;
    float capsuleLength = 2.0f * halfHeight;
    float halfCubeSide = 0.5f * cubeB->getScale();
    float capsuleRadius = capsuleA->getRadius();

    // Loop over the directions that are NOT parallel to face (there are two of them).
    // For each direction we'll raytrace along capsuleAxis to find where the axis passes
    // through the furthest face and then we'll clamp to remain on the capsule's line segment
    float shortestDistance = capsuleLength;

    for (int i = 0; i < 2; ++i) {
        int wallIndex = wallIndices[2 * faceIndex + i];
        // each direction has two walls: positive and negative
        for (float wallSign = -1.0f; wallSign < 2.0f; wallSign += 2.0f) {
            glm::vec3 wallNormal = wallSign * cubeNormals[wallIndex];
            float axisDotWall = glm::dot(capsuleAxis, wallNormal);
            if (axisDotWall > EPSILON) {
                // formula for distance to intersection between line (P,p) and plane (V,n) is: (V-P)*n / p*n
                float newDistance = (halfCubeSide - glm::dot(capsuleStart, wallNormal)) / axisDotWall;
                if (newDistance < 0.0f) {
                    // The wall is behind the capsule, but there is still a possibility that it collides 
                    // against the edge so we recast against the diagonal plane beteween the two faces.
                    // NOTE: it is impossible for the startPoint to be in front of the diagonal plane,
                    // therefore we know we'll get a valid distance.
                    glm::vec3 thirdNormal = glm::cross(faceNormal, wallNormal);
                    glm::vec3 diagonalNormal = glm::normalize(glm::cross(glm::cross(capsuleAxis, thirdNormal), thirdNormal));
                    newDistance = glm::dot(halfCubeSide * (faceNormal + wallNormal) - capsuleStart, diagonalNormal) / 
                        glm::dot(capsuleAxis, diagonalNormal);
                } else if (newDistance < capsuleLength) {
                    // The wall truncates the capsule axis, but we must check the case where the capsule
                    // collides with an edge/corner rather than the face.  The good news is that this gives us
                    // an opportunity to check for an early exit case.
                    float heightOfImpact = glm::dot(capsuleStart + newDistance * capsuleAxis, faceNormal);
                    if (heightOfImpact > halfCubeSide + SQUARE_ROOT_OF_2 * capsuleRadius) {
                        // it is impossible for the capsule to hit the face
                        return false;
                    } else {
                        // recast against the diagonal plane between the two faces
                        glm::vec3 thirdNormal = glm::cross(faceNormal, wallNormal);
                        glm::vec3 diagonalNormal = glm::normalize(glm::cross(glm::cross(capsuleAxis, thirdNormal), thirdNormal));
                        newDistance = glm::dot(halfCubeSide * (faceNormal + wallNormal) - capsuleStart, diagonalNormal) / 
                            glm::dot(capsuleAxis, diagonalNormal);
                    }
                }
                if (newDistance < shortestDistance) {
                    shortestDistance = newDistance;
                }
                // there can only be one hit per direction
                break;
            }
        }
    }
    
    // chose the point that produces the deepest penetration against face
    // and translate back into real frame
    glm::vec3 sphereCenter = cubeCenter + capsuleStart + shortestDistance * capsuleAxis;

    // collide like a sphere at point0 with capsuleRadius
    CollisionInfo* collision = sphereVsAACubeHelper(sphereCenter, capsuleRadius,
            cubeCenter, 2.0f * halfCubeSide, collisions);
    if (collision) {
        // we hit! so store back pointers to the shapes
        collision->_shapeA = shapeA;
        collision->_shapeB = shapeB;
        return true;
    }
    return false;
}

bool aaCubeVsSphere(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    return sphereVsAACube(shapeB, shapeA, collisions);
}

bool aaCubeVsCapsule(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    return capsuleVsAACube(shapeB, shapeA, collisions);
}

// helper function
CollisionInfo* aaCubeVsAACubeHelper(const glm::vec3& cubeCenterA, float cubeSideA, const glm::vec3& cubeCenterB, 
        float cubeSideB, CollisionList& collisions) {
    // cube is A
    // cube is B
    // BA = B - A = from center of A to center of B 
    float halfCubeSideA = 0.5f * cubeSideA;
    float halfCubeSideB = 0.5f * cubeSideB;
    glm::vec3 BA = cubeCenterB - cubeCenterA;

    float distance = glm::length(BA);

    if (distance > EPSILON) {
        float maxBA = glm::max(glm::max(glm::abs(BA.x), glm::abs(BA.y)), glm::abs(BA.z));
        if (maxBA > halfCubeSideB + halfCubeSideA) {
            // cube misses cube entirely
            return NULL;
        } 
        CollisionInfo* collision = collisions.getNewCollision();
        if (!collision) {
            return NULL; // no more room for collisions
        }
        if (maxBA > halfCubeSideB) {
            // cube hits cube but its center is outside cube
            // compute contact anti-pole on cube (in cube frame)
            glm::vec3 cubeContact = glm::abs(BA);
            if (cubeContact.x > halfCubeSideB) {
                cubeContact.x = halfCubeSideB;
            }
            if (cubeContact.y > halfCubeSideB) {
                cubeContact.y = halfCubeSideB;
            }
            if (cubeContact.z > halfCubeSideB) {
                cubeContact.z = halfCubeSideB;
            }
            glm::vec3 signs = glm::sign(BA);
            cubeContact.x *= signs.x;
            cubeContact.y *= signs.y;
            cubeContact.z *= signs.z;

            // compute penetration direction
            glm::vec3 direction = BA - cubeContact;
            
            float lengthDirection = glm::length(direction);
            
            if (lengthDirection < EPSILON) {
                // cubeCenterA is touching cube B surface, so we can't use the difference between those two 
                // points to compute the penetration direction.  Instead we use the unitary components of 
                // cubeContact.
                glm::modf(cubeContact / halfCubeSideB, direction);
                lengthDirection = glm::length(direction);
            } else if (lengthDirection > halfCubeSideA) {
                collisions.deleteLastCollision();
                return NULL;
            }
            direction /= lengthDirection;

            // compute collision details
            collision->_contactPoint = cubeCenterA + halfCubeSideA * direction;
            collision->_penetration = halfCubeSideA * direction - (BA - cubeContact);
        } else {
            // cube center is inside cube
            // --> push out nearest face
            glm::vec3 direction;
            BA /= maxBA;
            glm::modf(BA, direction);
            float lengthDirection = glm::length(direction);
            direction /= lengthDirection;

            // compute collision details
            collision->_floatData = cubeSideB;
            collision->_vecData = cubeCenterB;
            collision->_penetration = (halfCubeSideB * lengthDirection + halfCubeSideA - maxBA * glm::dot(BA, direction)) * direction;
            collision->_contactPoint = cubeCenterA + halfCubeSideA * direction;
        }
        collision->_shapeA = NULL;
        collision->_shapeB = NULL;
        return collision;
    } else if (halfCubeSideA + halfCubeSideB > distance) {
        // NOTE: for cocentric approximation we collide sphere and cube as two spheres which means 
        // this algorithm will probably be wrong when both sphere and cube are very small (both ~EPSILON)
        CollisionInfo* collision = collisions.getNewCollision();
        if (collision) {
            // the penetration and contactPoint are undefined, so we pick a penetration direction (-yAxis)
            collision->_penetration = (halfCubeSideA + halfCubeSideB) * glm::vec3(0.0f, -1.0f, 0.0f);
            // contactPoint is on surface of A
            collision->_contactPoint = cubeCenterA + collision->_penetration;
            collision->_shapeA = NULL;
            collision->_shapeB = NULL;
            return collision;
        }
    }
    return NULL;
}

bool aaCubeVsAACube(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    // BA = B - A = from center of A to center of B 
    const AACubeShape* cubeA = static_cast<const AACubeShape*>(shapeA);
    const AACubeShape* cubeB = static_cast<const AACubeShape*>(shapeB);
    CollisionInfo* collision = aaCubeVsAACubeHelper( cubeA->getTranslation(), cubeA->getScale(),
            cubeB->getTranslation(), cubeB->getScale(), collisions);
    if (collision) {
        collision->_shapeA = shapeA;
        collision->_shapeB = shapeB;
        return true;
    }
    return false;
}

bool shapeVsList(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    bool touching = false;
    const ListShape* listB = static_cast<const ListShape*>(shapeB);
    for (int i = 0; i < listB->size() && !collisions.isFull(); ++i) {
        const Shape* subShape = listB->getSubShape(i);
        touching = collideShapes(shapeA, subShape, collisions) || touching;
    }
    return touching;
}

bool listVsShape(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    bool touching = false;
    const ListShape* listA = static_cast<const ListShape*>(shapeA);
    for (int i = 0; i < listA->size() && !collisions.isFull(); ++i) {
        const Shape* subShape = listA->getSubShape(i);
        touching = collideShapes(subShape, shapeB, collisions) || touching;
    }
    return touching;
}

bool listVsList(const Shape* shapeA, const Shape* shapeB, CollisionList& collisions) {
    bool touching = false;
    const ListShape* listA = static_cast<const ListShape*>(shapeA);
    const ListShape* listB = static_cast<const ListShape*>(shapeB);
    for (int i = 0; i < listA->size() && !collisions.isFull(); ++i) {
        const Shape* subShape = listA->getSubShape(i);
        for (int j = 0; j < listB->size() && !collisions.isFull(); ++j) {
            touching = collideShapes(subShape, listB->getSubShape(j), collisions) || touching;
        }
    }
    return touching;
}

// helper function
/* KEEP THIS CODE -- this is how to collide the cube with stark face normals (no rounding).
* We might want to use this code later for sealing boundaries between adjacent voxels.
bool sphereAACube_StarkAngles(const glm::vec3& sphereCenter, float sphereRadius, const glm::vec3& cubeCenter, 
        float cubeSide, CollisionList& collisions) {
    glm::vec3 BA = cubeCenter - sphereCenter;
    float distance = glm::length(BA);
    if (distance > EPSILON) {
        BA /= distance; // BA is now normalized
        // compute the nearest point on sphere
        glm::vec3 surfaceA = sphereCenter + sphereRadius * BA;
        // compute the nearest point on cube
        float maxBA = glm::max(glm::max(fabsf(BA.x), fabsf(BA.y)), fabsf(BA.z));
        glm::vec3 surfaceB = cubeCenter - (0.5f * cubeSide / maxBA) * BA;
        // collision happens when "vector to surfaceA from surfaceB" dots with BA to produce a positive value
        glm::vec3 surfaceAB = surfaceA - surfaceB;
        if (glm::dot(surfaceAB, BA) > 0.0f) {
            CollisionInfo* collision = collisions.getNewCollision();
            if (collision) {
                // penetration is parallel to box side direction
                BA /= maxBA;
                glm::vec3 direction;
                glm::modf(BA, direction);
                direction = glm::normalize(direction); 

                // penetration is the projection of surfaceAB on direction
                collision->_penetration = glm::dot(surfaceAB, direction) * direction;
                // contactPoint is on surface of A
                collision->_contactPoint = sphereCenter + sphereRadius * direction;
                collision->_shapeA = NULL;
                collision->_shapeB = NULL;
                return true;
            }
        }
    } else if (sphereRadius + 0.5f * cubeSide > distance) {
        // NOTE: for cocentric approximation we collide sphere and cube as two spheres which means 
        // this algorithm will probably be wrong when both sphere and cube are very small (both ~EPSILON)
        CollisionInfo* collision = collisions.getNewCollision();
        if (collision) {
            // the penetration and contactPoint are undefined, so we pick a penetration direction (-yAxis)
            collision->_penetration = (sphereRadius + 0.5f * cubeSide) * glm::vec3(0.0f, -1.0f, 0.0f);
            // contactPoint is on surface of A
            collision->_contactPoint = sphereCenter + collision->_penetration;
            collision->_shapeA = NULL;
            collision->_shapeB = NULL;
            return true;
        }
    }
    return false;
}
*/

bool sphereVsAACubeLegacy(const SphereShape* sphereA, const glm::vec3& cubeCenter, float cubeSide, CollisionList& collisions) {
    return sphereVsAACubeLegacy(sphereA->getTranslation(), sphereA->getRadius(), cubeCenter, cubeSide, collisions);
}

bool capsuleVsAACubeLegacy(const CapsuleShape* capsuleA, const glm::vec3& cubeCenter, float cubeSide, CollisionList& collisions) {
    // find nerest approach of capsule line segment to cube
    glm::vec3 capsuleAxis;
    capsuleA->computeNormalizedAxis(capsuleAxis);
    float offset = glm::dot(cubeCenter - capsuleA->getTranslation(), capsuleAxis);
    float halfHeight = capsuleA->getHalfHeight();
    if (offset > halfHeight) {
        offset = halfHeight;
    } else if (offset < -halfHeight) {
        offset = -halfHeight;
    }
    glm::vec3 nearestApproach = capsuleA->getTranslation() + offset * capsuleAxis;
    // collide nearest approach like a sphere at that point
    return sphereVsAACubeLegacy(nearestApproach, capsuleA->getRadius(), cubeCenter, cubeSide, collisions);
}

bool findRayIntersection(const QVector<Shape*>& shapes, RayIntersectionInfo& intersection) {
    int numShapes = shapes.size();
    bool hit = false;
    for (int i = 0; i < numShapes; ++i) {
        Shape* shape = shapes.at(i);
        if (shape) {
            if (shape->findRayIntersection(intersection)) {
                hit = true;
            }
        }
    }
    return hit;
}

}   // namespace ShapeCollider
