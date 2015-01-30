//
//  Batch.cpp
//  interface/src/gpu
//
//  Created by Sam Gateau on 10/14/2014.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "Batch.h"

#include <QDebug>

#define ADD_COMMAND(call) _commands.push_back(COMMAND_##call); _commandOffsets.push_back(_params.size());

using namespace gpu;

Batch::Batch() :
    _commands(),
    _commandOffsets(),
    _params(),
    _resources(),
    _data(),
    _buffers(),
    _textures(),
    _streamFormats(),
    _transforms()
{
}

Batch::~Batch() {
}

void Batch::clear() {
    _commands.clear();
    _commandOffsets.clear();
    _params.clear();
    _resources.clear();
    _data.clear();
    _buffers.clear();
    _textures.clear();
    _streamFormats.clear();
    _transforms.clear();
}

uint32 Batch::cacheResource(Resource* res) {
    uint32 offset = _resources.size();
    _resources.push_back(ResourceCache(res));
    
    return offset;
}

uint32 Batch::cacheResource(const void* pointer) {
    uint32 offset = _resources.size();
    _resources.push_back(ResourceCache(pointer));

    return offset;
}

uint32 Batch::cacheData(uint32 size, const void* data) {
    uint32 offset = _data.size();
    uint32 nbBytes = size;
    _data.resize(offset + nbBytes);
    memcpy(_data.data() + offset, data, size);

    return offset;
}

void Batch::draw(Primitive primitiveType, uint32 nbVertices, uint32 startVertex) {
    ADD_COMMAND(draw);

    _params.push_back(startVertex);
    _params.push_back(nbVertices);
    _params.push_back(primitiveType);
}

void Batch::drawIndexed(Primitive primitiveType, uint32 nbIndices, uint32 startIndex) {
    ADD_COMMAND(drawIndexed);

    _params.push_back(startIndex);
    _params.push_back(nbIndices);
    _params.push_back(primitiveType);
}

void Batch::drawInstanced(uint32 nbInstances, Primitive primitiveType, uint32 nbVertices, uint32 startVertex, uint32 startInstance) {
    ADD_COMMAND(drawInstanced);

    _params.push_back(startInstance);
    _params.push_back(startVertex);
    _params.push_back(nbVertices);
    _params.push_back(primitiveType);
    _params.push_back(nbInstances);
}

void Batch::drawIndexedInstanced(uint32 nbInstances, Primitive primitiveType, uint32 nbIndices, uint32 startIndex, uint32 startInstance) {
    ADD_COMMAND(drawIndexedInstanced);

    _params.push_back(startInstance);
    _params.push_back(startIndex);
    _params.push_back(nbIndices);
    _params.push_back(primitiveType);
    _params.push_back(nbInstances);
}

void Batch::setInputFormat(const Stream::FormatPointer& format) {
    ADD_COMMAND(setInputFormat);

    _params.push_back(_streamFormats.cache(format));
}

void Batch::setInputBuffer(Slot channel, const BufferPointer& buffer, Offset offset, Offset stride) {
    ADD_COMMAND(setInputBuffer);

    _params.push_back(stride);
    _params.push_back(offset);
    _params.push_back(_buffers.cache(buffer));
    _params.push_back(channel);
}

void Batch::setInputBuffer(Slot channel, const BufferView& view) {
    setInputBuffer(channel, view._buffer, view._offset, Offset(view._stride));
}

void Batch::setInputStream(Slot startChannel, const BufferStream& stream) {
    if (stream.getNumBuffers()) {
        const Buffers& buffers = stream.getBuffers();
        const Offsets& offsets = stream.getOffsets();
        const Offsets& strides = stream.getStrides();
        for (unsigned int i = 0; i < buffers.size(); i++) {
            setInputBuffer(startChannel + i, buffers[i], offsets[i], strides[i]);
        }
    }
}

void Batch::setIndexBuffer(Type type, const BufferPointer& buffer, Offset offset) {
    ADD_COMMAND(setIndexBuffer);

    _params.push_back(offset);
    _params.push_back(_buffers.cache(buffer));
    _params.push_back(type);
}

void Batch::setModelTransform(const Transform& model) {
    ADD_COMMAND(setModelTransform);

    _params.push_back(_transforms.cache(model));
}

void Batch::setViewTransform(const Transform& view) {
    ADD_COMMAND(setViewTransform);

    _params.push_back(_transforms.cache(view));
}

void Batch::setProjectionTransform(const Transform& proj) {
    ADD_COMMAND(setProjectionTransform);

    _params.push_back(_transforms.cache(proj));
}

void Batch::setUniformBuffer(uint32 slot, const BufferPointer& buffer, Offset offset, Offset size) {
    ADD_COMMAND(setUniformBuffer);

    _params.push_back(size);
    _params.push_back(offset);
    _params.push_back(_buffers.cache(buffer));
    _params.push_back(slot);
}

void Batch::setUniformBuffer(uint32 slot, const BufferView& view) {
    setUniformBuffer(slot, view._buffer, view._offset, view._size);
}


void Batch::setUniformTexture(uint32 slot, const TexturePointer& texture) {
    ADD_COMMAND(setUniformTexture);

    _params.push_back(_textures.cache(texture));
    _params.push_back(slot);
}

void Batch::setUniformTexture(uint32 slot, const TextureView& view) {
    setUniformTexture(slot, view._texture);
}

