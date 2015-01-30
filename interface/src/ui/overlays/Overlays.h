//
//  Overlays.h
//  interface/src/ui/overlays
//
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Overlays_h
#define hifi_Overlays_h

#include <QString>
#include <QScriptValue>
#include <QSignalMapper>

#include "Base3DOverlay.h"
#include "Overlay.h"

class OverlayPropertyResult {
public:
    OverlayPropertyResult();
    QScriptValue value;
};

Q_DECLARE_METATYPE(OverlayPropertyResult);

QScriptValue OverlayPropertyResultToScriptValue(QScriptEngine* engine, const OverlayPropertyResult& value);
void OverlayPropertyResultFromScriptValue(const QScriptValue& object, OverlayPropertyResult& value);

class RayToOverlayIntersectionResult {
public:
    RayToOverlayIntersectionResult();
    bool intersects;
    int overlayID;
    float distance;
    BoxFace face;
    glm::vec3 intersection;
    QString extraInfo;
};


Q_DECLARE_METATYPE(RayToOverlayIntersectionResult);

QScriptValue RayToOverlayIntersectionResultToScriptValue(QScriptEngine* engine, const RayToOverlayIntersectionResult& value);
void RayToOverlayIntersectionResultFromScriptValue(const QScriptValue& object, RayToOverlayIntersectionResult& value);

class Overlays : public QObject {
    Q_OBJECT
public:
    Overlays();
    ~Overlays();
    void init(QGLWidget* parent);
    void update(float deltatime);
    void renderWorld(bool drawFront, RenderArgs::RenderMode renderMode = RenderArgs::DEFAULT_RENDER_MODE,
                        RenderArgs::RenderSide renderSide = RenderArgs::MONO);
    void renderHUD();

public slots:
    /// adds an overlay with the specific properties
    unsigned int addOverlay(const QString& type, const QScriptValue& properties);

    /// adds an overlay that's already been created
    unsigned int addOverlay(Overlay* overlay);

    /// clones an existing overlay
    unsigned int cloneOverlay(unsigned int id);

    /// edits an overlay updating only the included properties, will return the identified OverlayID in case of
    /// successful edit, if the input id is for an unknown overlay this function will have no effect
    bool editOverlay(unsigned int id, const QScriptValue& properties);

    /// deletes a particle
    void deleteOverlay(unsigned int id);

    /// returns the top most 2D overlay at the screen point, or 0 if not overlay at that point
    unsigned int getOverlayAtPoint(const glm::vec2& point);

    /// returns the value of specified property, or null if there is no such property
    OverlayPropertyResult getProperty(unsigned int id, const QString& property);

    /// returns details about the closest 3D Overlay hit by the pick ray
    RayToOverlayIntersectionResult findRayIntersection(const PickRay& ray);
    
    /// returns whether the overlay's assets are loaded or not
    bool isLoaded(unsigned int id);

    /// returns the size of the given text in the specified overlay if it is a text overlay: in pixels if it is a 2D text
    /// overlay; in meters if it is a 3D text overlay
    QSizeF textSize(unsigned int id, const QString& text) const;

private:
    QMap<unsigned int, Overlay*> _overlaysHUD;
    QMap<unsigned int, Overlay*> _overlaysWorld;
    QList<Overlay*> _overlaysToDelete;
    unsigned int _nextOverlayID;
    QGLWidget* _parent;
    QReadWriteLock _lock;
    QReadWriteLock _deleteLock;
    QScriptEngine* _scriptEngine;
};


 
#endif // hifi_Overlays_h
