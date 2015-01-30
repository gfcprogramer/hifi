//
//  GLBackend.h
//  libraries/gpu/src/gpu
//
//  Created by Sam Gateau on 10/27/2014.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#ifndef hifi_gpu_GLBackend_h
#define hifi_gpu_GLBackend_h

#include <assert.h>
#include "GPUConfig.h"

#include "Context.h"
#include "Batch.h"
#include <bitset>


namespace gpu {

class GLBackend : public Backend {
public:

    GLBackend();
    ~GLBackend();

    static void renderBatch(Batch& batch);

    static void checkGLError();


    class GLBuffer {
    public:
        Stamp _stamp;
        GLuint _buffer;
        GLuint _size;

        GLBuffer();
        ~GLBuffer();
    };
    static void syncGPUObject(const Buffer& buffer);
    static GLuint getBufferID(const Buffer& buffer);

    class GLTexture {
    public:
        Stamp _storageStamp;
        Stamp _contentStamp;
        GLuint _texture;
        GLuint _size;

        GLTexture();
        ~GLTexture();
    };
    static void syncGPUObject(const Texture& texture);
    static GLuint getTextureID(const TexturePointer& texture);

    static const int MAX_NUM_ATTRIBUTES = Stream::NUM_INPUT_SLOTS;
    static const int MAX_NUM_INPUT_BUFFERS = 16;

    uint32 getNumInputBuffers() const { return _input._buffersState.size(); }

protected:

    // Draw Stage
    void do_draw(Batch& batch, uint32 paramOffset);
    void do_drawIndexed(Batch& batch, uint32 paramOffset);
    void do_drawInstanced(Batch& batch, uint32 paramOffset);
    void do_drawIndexedInstanced(Batch& batch, uint32 paramOffset);

    // Input Stage
    void do_setInputFormat(Batch& batch, uint32 paramOffset);
    void do_setInputBuffer(Batch& batch, uint32 paramOffset);
    void do_setIndexBuffer(Batch& batch, uint32 paramOffset);

    void updateInput();
    struct InputStageState {
        bool _invalidFormat;
        Stream::FormatPointer _format;

        typedef std::bitset<MAX_NUM_INPUT_BUFFERS> BuffersState;
        BuffersState _buffersState;

        Buffers _buffers;
        Offsets _bufferOffsets;
        Offsets _bufferStrides;

        BufferPointer _indexBuffer;
        Offset _indexBufferOffset;
        Type _indexBufferType;

        typedef std::bitset<MAX_NUM_ATTRIBUTES> ActivationCache;
        ActivationCache _attributeActivation;

        InputStageState() :
            _invalidFormat(true),
            _format(0),
            _buffersState(0),
            _buffers(_buffersState.size(), BufferPointer(0)),
            _bufferOffsets(_buffersState.size(), 0),
            _bufferStrides(_buffersState.size(), 0),
            _indexBuffer(0),
            _indexBufferOffset(0),
            _indexBufferType(UINT32),
            _attributeActivation(0)
             {}
    } _input;

    // Transform Stage
    void do_setModelTransform(Batch& batch, uint32 paramOffset);
    void do_setViewTransform(Batch& batch, uint32 paramOffset);
    void do_setProjectionTransform(Batch& batch, uint32 paramOffset);

    void updateTransform();
    struct TransformStageState {
        Transform _model;
        Transform _view;
        Transform _projection;
        bool _invalidModel;
        bool _invalidView;
        bool _invalidProj;

        GLenum _lastMode;

        TransformStageState() :
            _model(),
            _view(),
            _projection(),
            _invalidModel(true),
            _invalidView(true),
            _invalidProj(true),
            _lastMode(GL_TEXTURE) {}
    } _transform;

    // Shader Stage
    void do_setUniformBuffer(Batch& batch, uint32 paramOffset);
    void do_setUniformTexture(Batch& batch, uint32 paramOffset);
 
    void updateShader();
    struct ShaderStageState {

        GLuint _program;

        ShaderStageState() :
            _program(0) {}
    } _shader;

 
    // TODO: As long as we have gl calls explicitely issued from interface
    // code, we need to be able to record and batch these calls. THe long 
    // term strategy is to get rid of any GL calls in favor of the HIFI GPU API
    void do_glEnable(Batch& batch, uint32 paramOffset);
    void do_glDisable(Batch& batch, uint32 paramOffset);

    void do_glEnableClientState(Batch& batch, uint32 paramOffset);
    void do_glDisableClientState(Batch& batch, uint32 paramOffset);

    void do_glCullFace(Batch& batch, uint32 paramOffset);
    void do_glAlphaFunc(Batch& batch, uint32 paramOffset);

    void do_glDepthFunc(Batch& batch, uint32 paramOffset);
    void do_glDepthMask(Batch& batch, uint32 paramOffset);
    void do_glDepthRange(Batch& batch, uint32 paramOffset);

    void do_glBindBuffer(Batch& batch, uint32 paramOffset);

    void do_glBindTexture(Batch& batch, uint32 paramOffset);
    void do_glActiveTexture(Batch& batch, uint32 paramOffset);

    void do_glDrawBuffers(Batch& batch, uint32 paramOffset);

    void do_glUseProgram(Batch& batch, uint32 paramOffset);
    void do_glUniform1f(Batch& batch, uint32 paramOffset);
    void do_glUniform2f(Batch& batch, uint32 paramOffset);
    void do_glUniform4fv(Batch& batch, uint32 paramOffset);
    void do_glUniformMatrix4fv(Batch& batch, uint32 paramOffset);

    void do_glMatrixMode(Batch& batch, uint32 paramOffset);
    void do_glPushMatrix(Batch& batch, uint32 paramOffset);
    void do_glPopMatrix(Batch& batch, uint32 paramOffset);
    void do_glMultMatrixf(Batch& batch, uint32 paramOffset);
    void do_glLoadMatrixf(Batch& batch, uint32 paramOffset);
    void do_glLoadIdentity(Batch& batch, uint32 paramOffset);
    void do_glRotatef(Batch& batch, uint32 paramOffset);
    void do_glScalef(Batch& batch, uint32 paramOffset);
    void do_glTranslatef(Batch& batch, uint32 paramOffset);

    void do_glDrawArrays(Batch& batch, uint32 paramOffset);
    void do_glDrawRangeElements(Batch& batch, uint32 paramOffset);

    void do_glColorPointer(Batch& batch, uint32 paramOffset);
    void do_glNormalPointer(Batch& batch, uint32 paramOffset);
    void do_glTexCoordPointer(Batch& batch, uint32 paramOffset);
    void do_glVertexPointer(Batch& batch, uint32 paramOffset);

    void do_glVertexAttribPointer(Batch& batch, uint32 paramOffset);
    void do_glEnableVertexAttribArray(Batch& batch, uint32 paramOffset);
    void do_glDisableVertexAttribArray(Batch& batch, uint32 paramOffset);

    void do_glColor4f(Batch& batch, uint32 paramOffset);

    typedef void (GLBackend::*CommandCall)(Batch&, uint32);
    static CommandCall _commandCalls[Batch::NUM_COMMANDS];

};


};

#endif
