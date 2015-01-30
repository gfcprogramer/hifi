//
//  DependencyManager.cpp
//
//
//  Created by Clément Brisset on 12/10/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "DependencyManager.h"

DependencyManager DependencyManager::_manager;

QSharedPointer<Dependency>& DependencyManager::safeGet(size_t hashCode) {
    return _instanceHash[hashCode];
}