//
//  BillboardOverlay.h
//
//
//  Created by Clement on 7/1/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_BillboardOverlay_h
#define hifi_BillboardOverlay_h

#include <QScopedPointer>
#include <QUrl>

#include <TextureCache.h>

#include "Base3DOverlay.h"

class BillboardOverlay : public Base3DOverlay {
    Q_OBJECT
public:
    BillboardOverlay();
    BillboardOverlay(const BillboardOverlay* billboardOverlay);

    virtual void render(RenderArgs* args);

    // setters
    void setURL(const QString& url);
    void setScale(float scale) { _scale = scale; }
    void setIsFacingAvatar(bool isFacingAvatar) { _isFacingAvatar = isFacingAvatar; }

    virtual void setProperties(const QScriptValue& properties);
    void setClipFromSource(const QRect& bounds) { _fromImage = bounds; }
    virtual QScriptValue getProperty(const QString& property);

    virtual bool findRayIntersection(const glm::vec3& origin, const glm::vec3& direction, float& distance, BoxFace& face);
    
    virtual BillboardOverlay* createClone() const;

private slots:
    void replyFinished();

private:
    void setBillboardURL(const QString& url);
    
    QString _url;
    QByteArray _billboard;
    QSize _size;
    QScopedPointer<Texture> _billboardTexture;
    bool _newTextureNeeded;
    
    QRect _fromImage; // where from in the image to sample

    float _scale;
    bool _isFacingAvatar;
};

#endif // hifi_BillboardOverlay_h