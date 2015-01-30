//
//  TextureCache.h
//  interface/src/renderer
//
//  Created by Andrzej Kapolka on 8/6/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_TextureCache_h
#define hifi_TextureCache_h

#include <gpu/GPUConfig.h>
#include <gpu/Texture.h>

#include <QImage>
#include <QMap>
#include <QGLWidget>

#include <DependencyManager.h>
#include <ResourceCache.h>

class QOpenGLFramebufferObject;

class NetworkTexture;

typedef QSharedPointer<NetworkTexture> NetworkTexturePointer;

enum TextureType { DEFAULT_TEXTURE, NORMAL_TEXTURE, SPECULAR_TEXTURE, EMISSIVE_TEXTURE, SPLAT_TEXTURE };

/// Stores cached textures, including render-to-texture targets.
class TextureCache : public ResourceCache, public Dependency {
    Q_OBJECT
    SINGLETON_DEPENDENCY
    
public:

    void associateWithWidget(QGLWidget* widget);
    
    /// Sets the desired texture resolution for the framebuffer objects. 
    void setFrameBufferSize(QSize frameBufferSize);
    const QSize& getFrameBufferSize() const { return _frameBufferSize; } 

    /// Returns the ID of the permutation/normal texture used for Perlin noise shader programs.  This texture
    /// has two lines: the first, a set of random numbers in [0, 255] to be used as permutation offsets, and
    /// the second, a set of random unit vectors to be used as noise gradients.
    const gpu::TexturePointer& getPermutationNormalTexture();

    /// Returns an opaque white texture (useful for a default).
    const gpu::TexturePointer& getWhiteTexture();

    /// Returns the a pale blue texture (useful for a normal map).
    const gpu::TexturePointer& getBlueTexture();

    /// Loads a texture from the specified URL.
    NetworkTexturePointer getTexture(const QUrl& url, TextureType type = DEFAULT_TEXTURE, bool dilatable = false,
        const QByteArray& content = QByteArray());

    /// Returns a pointer to the primary framebuffer object.  This render target includes a depth component, and is
    /// used for scene rendering.
    QOpenGLFramebufferObject* getPrimaryFramebufferObject();
    
    /// Returns the ID of the primary framebuffer object's depth texture.  This contains the Z buffer used in rendering.
    GLuint getPrimaryDepthTextureID();
    
    /// Returns the ID of the primary framebuffer object's normal texture.
    GLuint getPrimaryNormalTextureID();
    
    /// Returns the ID of the primary framebuffer object's specular texture.
    GLuint getPrimarySpecularTextureID();

    /// Enables or disables draw buffers on the primary framebuffer.  Note: the primary framebuffer must be bound.
    void setPrimaryDrawBuffers(bool color, bool normal = false, bool specular = false);
    
    /// Returns a pointer to the secondary framebuffer object, used as an additional render target when performing full
    /// screen effects.
    QOpenGLFramebufferObject* getSecondaryFramebufferObject();
    
    /// Returns a pointer to the tertiary framebuffer object, used as an additional render target when performing full
    /// screen effects.
    QOpenGLFramebufferObject* getTertiaryFramebufferObject();
    
    /// Returns a pointer to the framebuffer object used to render shadow maps.
    QOpenGLFramebufferObject* getShadowFramebufferObject();
    
    /// Returns the ID of the shadow framebuffer object's depth texture.
    GLuint getShadowDepthTextureID();
    
    virtual bool eventFilter(QObject* watched, QEvent* event);

protected:

    virtual QSharedPointer<Resource> createResource(const QUrl& url,
        const QSharedPointer<Resource>& fallback, bool delayLoad, const void* extra);
        
private:
    TextureCache();
    virtual ~TextureCache();
    friend class DilatableNetworkTexture;
    
    QOpenGLFramebufferObject* createFramebufferObject();
 
    gpu::TexturePointer _permutationNormalTexture;
    gpu::TexturePointer _whiteTexture;
    gpu::TexturePointer _blueTexture;
    
    QHash<QUrl, QWeakPointer<NetworkTexture> > _dilatableNetworkTextures;
    
    GLuint _primaryDepthTextureID;
    GLuint _primaryNormalTextureID;
    GLuint _primarySpecularTextureID;
    QOpenGLFramebufferObject* _primaryFramebufferObject;
    QOpenGLFramebufferObject* _secondaryFramebufferObject;
    QOpenGLFramebufferObject* _tertiaryFramebufferObject;
    
    QOpenGLFramebufferObject* _shadowFramebufferObject;
    GLuint _shadowDepthTextureID;

    QSize _frameBufferSize;
    QGLWidget* _associatedWidget;
};

/// A simple object wrapper for an OpenGL texture.
class Texture {
public:
    friend class TextureCache;
    friend class DilatableNetworkTexture;
    Texture();
    ~Texture();

    GLuint getID() const;

    const gpu::TexturePointer& getGPUTexture() const { return _gpuTexture; }

protected:
    gpu::TexturePointer _gpuTexture;

private:
};

/// A texture loaded from the network.
class NetworkTexture : public Resource, public Texture {
    Q_OBJECT

public:
    
    NetworkTexture(const QUrl& url, TextureType type, const QByteArray& content);

    /// Checks whether it "looks like" this texture is translucent
    /// (majority of pixels neither fully opaque or fully transparent).
    bool isTranslucent() const { return _translucent; }

    /// Returns the lazily-computed average texture color.
    const QColor& getAverageColor() const { return _averageColor; }

    int getOriginalWidth() const { return _originalWidth; }
    int getOriginalHeight() const { return _originalHeight; }
    int getWidth() const { return _width; }
    int getHeight() const { return _height; }

protected:

    virtual void downloadFinished(QNetworkReply* reply);
          
    Q_INVOKABLE void loadContent(const QByteArray& content);
    Q_INVOKABLE void setImage(const QImage& image, bool translucent, const QColor& averageColor, int originalWidth,
                              int originalHeight);

    virtual void imageLoaded(const QImage& image);

    TextureType _type;

private:
    bool _translucent;
    QColor _averageColor;
    int _originalWidth;
    int _originalHeight;
    int _width;
    int _height;
};

/// Caches derived, dilated textures.
class DilatableNetworkTexture : public NetworkTexture {
    Q_OBJECT
    
public:
    
    DilatableNetworkTexture(const QUrl& url, const QByteArray& content);
    
    /// Returns a pointer to a texture with the requested amount of dilation.
    QSharedPointer<Texture> getDilatedTexture(float dilation);
    
protected:

    virtual void imageLoaded(const QImage& image);
    virtual void reinsert();
    
private:
    
    QImage _image;
    int _innerRadius;
    int _outerRadius;
    
    QMap<float, QWeakPointer<Texture> > _dilatedTextures;    
};

#endif // hifi_TextureCache_h
