//
//  EntitySimulation.h
//  libraries/entities/src
//
//  Created by Andrew Meadows on 2014.11.24
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_EntitySimulation_h
#define hifi_EntitySimulation_h

#include <QtCore/QObject>
#include <QSet>

#include <PerfStat.h>

#include "EntityItem.h"
#include "EntityTree.h"

// the EntitySimulation needs to know when these things change on an entity, 
// so it can sort EntityItem or relay its state to the PhysicsEngine.
const int DIRTY_SIMULATION_FLAGS = 
        EntityItem::DIRTY_POSITION |
        EntityItem::DIRTY_VELOCITY |
        EntityItem::DIRTY_MASS |
        EntityItem::DIRTY_COLLISION_GROUP |
        EntityItem::DIRTY_MOTION_TYPE |
        EntityItem::DIRTY_SHAPE |
        EntityItem::DIRTY_LIFETIME |
        EntityItem::DIRTY_UPDATEABLE;

class EntitySimulation : public QObject {
Q_OBJECT
public:
    EntitySimulation() : _mutex(QMutex::Recursive), _entityTree(NULL) { }
    virtual ~EntitySimulation() { setEntityTree(NULL); }

    void lock() { _mutex.lock(); }
    void unlock() { _mutex.unlock(); }

    /// \param tree pointer to EntityTree which is stored internally
    void setEntityTree(EntityTree* tree);

    /// \param[out] entitiesToDelete list of entities removed from simulation and should be deleted.
    void updateEntities(QSet<EntityItem*>& entitiesToDelete);

    /// \param entity pointer to EntityItem to add to the simulation
    /// \sideeffect the EntityItem::_simulationState member may be updated to indicate membership to internal list
    void addEntity(EntityItem* entity);

    /// \param entity pointer to EntityItem to removed from the simulation
    /// \sideeffect the EntityItem::_simulationState member may be updated to indicate non-membership to internal list
    void removeEntity(EntityItem* entity);

    /// \param entity pointer to EntityItem to that may have changed in a way that would affect its simulation
    /// call this whenever an entity was changed from some EXTERNAL event (NOT by the EntitySimulation itself)
    void entityChanged(EntityItem* entity);

    void clearEntities();

    EntityTree* getEntityTree() { return _entityTree; }

signals:
    void entityCollisionWithEntity(const EntityItemID& idA, const EntityItemID& idB, const Collision& collision);

protected:

    // These pure virtual methods are protected because they are not to be called will-nilly. The base class
    // calls them in the right places.

    // NOTE: updateEntitiesInternal() should clear all dirty flags on each changed entity as side effect
    virtual void updateEntitiesInternal(const quint64& now) = 0;

    virtual void addEntityInternal(EntityItem* entity) = 0;

    virtual void removeEntityInternal(EntityItem* entity) = 0;

    virtual void entityChangedInternal(EntityItem* entity) = 0;

    virtual void sortEntitiesThatMovedInternal() {}

    virtual void clearEntitiesInternal() = 0;

    void expireMortalEntities(const quint64& now);
    void callUpdateOnEntitiesThatNeedIt(const quint64& now);
    void sortEntitiesThatMoved();

    QMutex _mutex;

    // back pointer to EntityTree structure
    EntityTree* _entityTree;

    // We maintain multiple lists, each for its distinct purpose.
    // An entity may be in more than one list.
    QSet<EntityItem*> _mortalEntities; // entities that have an expiry
    quint64 _nextExpiry;
    QSet<EntityItem*> _updateableEntities; // entities that need update() called
    QSet<EntityItem*> _entitiesToBeSorted; // entities that were moved by THIS simulation and might need to be resorted in the tree
    QSet<EntityItem*> _entitiesToDelete;
};

#endif // hifi_EntitySimulation_h
