//
//  MetavoxelSystem.h
//  interface/src
//
//  Created by Andrzej Kapolka on 12/10/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MetavoxelSystem_h
#define hifi_MetavoxelSystem_h

#include <QList>
#include <QOpenGLBuffer>
#include <QReadWriteLock>
#include <QVector>

#include <glm/glm.hpp>

#include <MetavoxelClientManager.h>
#include <ProgramObject.h>
#include <TextureCache.h>

class HeightfieldBaseLayerBatch;
class HeightfieldSplatBatch;
class HermiteBatch;
class MetavoxelBatch;
class Model;
class VoxelSplatBatch;

/// Renders a metavoxel tree.
class MetavoxelSystem : public MetavoxelClientManager {
    Q_OBJECT

public:

    class NetworkSimulation {
    public:
        float dropRate;
        float repeatRate;
        int minimumDelay;
        int maximumDelay;
        int bandwidthLimit;
        
        NetworkSimulation(float dropRate = 0.0f, float repeatRate = 0.0f, int minimumDelay = 0,
            int maximumDelay = 0, int bandwidthLimit = 0);
    };

    virtual ~MetavoxelSystem();

    virtual void init();

    virtual MetavoxelLOD getLOD();

    const Frustum& getFrustum() const { return _frustum; }
    
    void setNetworkSimulation(const NetworkSimulation& simulation);
    NetworkSimulation getNetworkSimulation();
    
    void simulate(float deltaTime);
    void render();

    void renderHeightfieldCursor(const glm::vec3& position, float radius);

    Q_INVOKABLE void paintHeightfieldColor(const glm::vec3& position, float radius, const QColor& color);

    Q_INVOKABLE void paintHeightfieldMaterial(const glm::vec3& position, float radius, const SharedObjectPointer& material);
        
    Q_INVOKABLE void setHeightfieldColor(const SharedObjectPointer& spanner, const QColor& color, bool paint = false);
        
    Q_INVOKABLE void setHeightfieldMaterial(const SharedObjectPointer& spanner,
        const SharedObjectPointer& material, bool paint = false);
    
    void addHeightfieldBaseBatch(const HeightfieldBaseLayerBatch& batch) { _heightfieldBaseBatches.append(batch); }
    void addHeightfieldSplatBatch(const HeightfieldSplatBatch& batch) { _heightfieldSplatBatches.append(batch); }
    
    void addVoxelBaseBatch(const MetavoxelBatch& batch) { _voxelBaseBatches.append(batch); }
    void addVoxelSplatBatch(const VoxelSplatBatch& batch) { _voxelSplatBatches.append(batch); }
    
    void addHermiteBatch(const HermiteBatch& batch) { _hermiteBatches.append(batch); }

    Q_INVOKABLE void deleteTextures(int heightTextureID, int colorTextureID, int materialTextureID) const;
    Q_INVOKABLE void deleteBuffers(int vertexBufferID, int indexBufferID, int hermiteBufferID) const;
    
signals:

    void rendering();

protected:

    Q_INVOKABLE void applyMaterialEdit(const MetavoxelEditMessage& message, bool reliable = false);

    virtual MetavoxelClient* createClient(const SharedNodePointer& node);

private:
    
    void guideToAugmented(MetavoxelVisitor& visitor, bool render = false);
    
    MetavoxelLOD _lod;
    QReadWriteLock _lodLock;
    Frustum _frustum;
    
    NetworkSimulation _networkSimulation;
    QReadWriteLock _networkSimulationLock;
    
    QVector<HeightfieldBaseLayerBatch> _heightfieldBaseBatches;
    QVector<HeightfieldSplatBatch> _heightfieldSplatBatches;
    QVector<MetavoxelBatch> _voxelBaseBatches;
    QVector<VoxelSplatBatch> _voxelSplatBatches;
    QVector<HermiteBatch> _hermiteBatches;
    
    ProgramObject _baseHeightfieldProgram;
    int _baseHeightScaleLocation;
    int _baseColorScaleLocation;
    
    class SplatLocations {
    public:
        int heightScale;
        int textureScale;
        int splatTextureOffset;
        int splatTextureScalesS;
        int splatTextureScalesT;
        int textureValueMinima;
        int textureValueMaxima;
        int materials;
        int materialWeights;
    };
    
    ProgramObject _splatHeightfieldProgram;
    SplatLocations _splatHeightfieldLocations;
    
    int _splatHeightScaleLocation;
    int _splatTextureScaleLocation;
    int _splatTextureOffsetLocation;
    int _splatTextureScalesSLocation;
    int _splatTextureScalesTLocation;
    int _splatTextureValueMinimaLocation;
    int _splatTextureValueMaximaLocation;
    
    ProgramObject _heightfieldCursorProgram;
    
    ProgramObject _baseVoxelProgram;
    ProgramObject _splatVoxelProgram;
    SplatLocations _splatVoxelLocations;
    
    ProgramObject _voxelCursorProgram;
    
    static void loadSplatProgram(const char* type, ProgramObject& program, SplatLocations& locations);
};

/// Base class for all batches.
class MetavoxelBatch {
public:
    GLuint vertexBufferID;
    GLuint indexBufferID;
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
    int vertexCount;
    int indexCount;
};

/// Base class for heightfield batches.
class HeightfieldBatch : public MetavoxelBatch {
public:
    GLuint heightTextureID;
    glm::vec4 heightScale;
};

/// A batch containing a heightfield base layer. 
class HeightfieldBaseLayerBatch : public HeightfieldBatch {
public:
    GLuint colorTextureID;
    glm::vec2 colorScale;
};

/// A batch containing a heightfield splat.
class HeightfieldSplatBatch : public HeightfieldBatch {
public:
    GLuint materialTextureID;
    glm::vec2 textureScale;
    glm::vec2 splatTextureOffset;
    int splatTextureIDs[4];
    glm::vec4 splatTextureScalesS;
    glm::vec4 splatTextureScalesT;
    int materialIndex;
};

/// A batch containing a voxel splat.
class VoxelSplatBatch : public MetavoxelBatch {
public:
    glm::vec3 splatTextureOffset;
    int splatTextureIDs[4];
    glm::vec4 splatTextureScalesS;
    glm::vec4 splatTextureScalesT;
    int materialIndex;
};

/// A batch containing Hermite data for debugging.
class HermiteBatch {
public:
    GLuint vertexBufferID;
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
    int vertexCount;
};

/// Generic abstract base class for objects that handle a signal.
class SignalHandler : public QObject {
    Q_OBJECT
    
public slots:
    
    virtual void handle() = 0;
};

/// Simple throttle for limiting bandwidth on a per-second basis.
class Throttle {
public:
    
    Throttle();
    
    /// Sets the per-second limit.
    void setLimit(int limit) { _limit = limit; }
    
    /// Determines whether the message with the given size should be throttled (discarded).  If not, registers the message
    /// as having been processed (i.e., contributing to later throttling).
    bool shouldThrottle(int bytes);

private:
    
    int _limit;
    int _total;
    
    typedef QPair<qint64, int> Bucket;
    QList<Bucket> _buckets;
};

/// A client session associated with a single server.
class MetavoxelSystemClient : public MetavoxelClient {
    Q_OBJECT    
    
public:
    
    MetavoxelSystemClient(const SharedNodePointer& node, MetavoxelUpdater* updater);
    
    Q_INVOKABLE void setAugmentedData(const MetavoxelData& data);
    
    /// Returns a copy of the augmented data.  This function is thread-safe.
    MetavoxelData getAugmentedData();
    
    void setRenderedAugmentedData(const MetavoxelData& data) { _renderedAugmentedData = data; }

    virtual int parseData(const QByteArray& packet);
    
protected:
    
    virtual void dataChanged(const MetavoxelData& oldData);
    virtual void sendDatagram(const QByteArray& data);

private:
    
    MetavoxelData _augmentedData;
    MetavoxelData _renderedAugmentedData;
    QReadWriteLock _augmentedDataLock;
    
    Throttle _sendThrottle;
    Throttle _receiveThrottle;
};

/// Base class for cached static buffers.
class BufferData : public QSharedData {
public:
    
    virtual ~BufferData();

    virtual void render(const glm::vec3& translation, const glm::quat& rotation,
        const glm::vec3& scale, bool cursor = false) = 0;
};

typedef QExplicitlySharedDataPointer<BufferData> BufferDataPointer;

/// Describes contents of a vertex in a voxel buffer.
class VoxelPoint {
public:
    glm::vec3 vertex;
    quint8 color[3];
    char normal[3];
    quint8 materials[4];
    quint8 materialWeights[4];
    
    void setNormal(const glm::vec3& normal);
};

/// A container for a coordinate within a voxel block.
class VoxelCoord {
public:
    QRgb encoded;
    
    VoxelCoord(QRgb encoded) : encoded(encoded) { }
    
    bool operator==(const VoxelCoord& other) const { return encoded == other.encoded; }
};

inline uint qHash(const VoxelCoord& coord, uint seed) {
    // multiply by prime numbers greater than the possible size
    return qHash(qRed(coord.encoded) + 257 * (qGreen(coord.encoded) + 263 * qBlue(coord.encoded)), seed);
}

/// Contains the information necessary to render a voxel block.
class VoxelBuffer : public BufferData {
public:
    
    VoxelBuffer(const QVector<VoxelPoint>& vertices, const QVector<int>& indices, const QVector<glm::vec3>& hermite,
        const QMultiHash<VoxelCoord, int>& quadIndices, int size, const QVector<SharedObjectPointer>& materials =
            QVector<SharedObjectPointer>());
    virtual ~VoxelBuffer();

    bool isHermiteEnabled() const { return _hermiteEnabled; }

    /// Finds the first intersection between the described ray and the voxel data.
    bool findRayIntersection(const glm::vec3& origin, const glm::vec3& direction, float boundsDistance, float& distance) const;
        
    virtual void render(const glm::vec3& translation, const glm::quat& rotation,
        const glm::vec3& scale, bool cursor = false);

private:
    
    QVector<VoxelPoint> _vertices;
    QVector<int> _indices;
    QVector<glm::vec3> _hermite;
    bool _hermiteEnabled;
    QMultiHash<VoxelCoord, int> _quadIndices;
    int _size;
    int _vertexCount;
    int _indexCount;
    int _hermiteCount;
    GLuint _vertexBufferID;
    GLuint _indexBufferID;
    GLuint _hermiteBufferID;
    QVector<SharedObjectPointer> _materials;
    QVector<NetworkTexturePointer> _networkTextures;
};

/// Renders metavoxels as points.
class DefaultMetavoxelRendererImplementation : public MetavoxelRendererImplementation {
    Q_OBJECT

public:
    
    Q_INVOKABLE DefaultMetavoxelRendererImplementation();
    
    virtual void simulate(MetavoxelData& data, float deltaTime, MetavoxelInfo& info, const MetavoxelLOD& lod);
    virtual void render(MetavoxelData& data, MetavoxelInfo& info, const MetavoxelLOD& lod);
};

/// Renders spheres.
class SphereRenderer : public SpannerRenderer {
    Q_OBJECT

public:
    
    Q_INVOKABLE SphereRenderer();
    
    virtual void render(const MetavoxelLOD& lod = MetavoxelLOD(), bool contained = false, bool cursor = false);
};

/// Renders cuboids.
class CuboidRenderer : public SpannerRenderer {
    Q_OBJECT

public:
    
    Q_INVOKABLE CuboidRenderer();
    
    virtual void render(const MetavoxelLOD& lod = MetavoxelLOD(), bool contained = false, bool cursor = false);
};

/// Renders static models.
class StaticModelRenderer : public SpannerRenderer {
    Q_OBJECT

public:
    
    Q_INVOKABLE StaticModelRenderer();
    
    virtual void init(Spanner* spanner);
    virtual void simulate(float deltaTime);
    virtual void render(const MetavoxelLOD& lod = MetavoxelLOD(), bool contained = false, bool cursor = false);
    virtual bool findRayIntersection(const glm::vec3& origin, const glm::vec3& direction, float& distance) const;

private slots:

    void applyTranslation(const glm::vec3& translation);
    void applyRotation(const glm::quat& rotation);
    void applyScale(float scale);
    void applyURL(const QUrl& url);

private:
    
    Model* _model;
};

/// Renders heightfields.
class HeightfieldRenderer : public SpannerRenderer {
    Q_OBJECT

public:
    
    Q_INVOKABLE HeightfieldRenderer();
    
    virtual void render(const MetavoxelLOD& lod = MetavoxelLOD(), bool contained = false, bool cursor = false);
};

/// Renders a single quadtree node.
class HeightfieldNodeRenderer : public AbstractHeightfieldNodeRenderer {
public:
    
    HeightfieldNodeRenderer();
    virtual ~HeightfieldNodeRenderer();
    
    virtual bool findRayIntersection(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale,
        const glm::vec3& origin, const glm::vec3& direction, float boundsDistance, float& distance) const; 
        
    void render(const HeightfieldNodePointer& node, const glm::vec3& translation,
        const glm::quat& rotation, const glm::vec3& scale, bool cursor);
    
private:

    GLuint _heightTextureID;
    GLuint _colorTextureID;
    GLuint _materialTextureID;
    QVector<NetworkTexturePointer> _networkTextures;
    
    BufferDataPointer _voxels;
    
    typedef QPair<int, int> IntPair;    
    typedef QPair<QOpenGLBuffer, QOpenGLBuffer> BufferPair;
    static QHash<IntPair, BufferPair> _bufferPairs;
};

#endif // hifi_MetavoxelSystem_h
