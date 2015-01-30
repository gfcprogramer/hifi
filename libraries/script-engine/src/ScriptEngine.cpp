//
//  ScriptEngine.cpp
//  libraries/script-engine/src
//
//  Created by Brad Hefta-Gaub on 12/14/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QScriptEngine>

#include <AudioConstants.h>
#include <AudioEffectOptions.h>
#include <AudioInjector.h>
#include <AvatarData.h>
#include <Bitstream.h>
#include <CollisionInfo.h>
#include <EntityScriptingInterface.h>
#include <NetworkAccessManager.h>
#include <NodeList.h>
#include <PacketHeaders.h>
#include <UUID.h>

#include "AnimationObject.h"
#include "ArrayBufferViewClass.h"
#include "BatchLoader.h"
#include "DataViewClass.h"
#include "EventTypes.h"
#include "MenuItemProperties.h"
#include "ScriptEngine.h"
#include "TypedArrays.h"
#include "XMLHttpRequestClass.h"

#include "MIDIEvent.h"

EntityScriptingInterface ScriptEngine::_entityScriptingInterface;

static QScriptValue debugPrint(QScriptContext* context, QScriptEngine* engine){
    qDebug() << "script:print()<<" << context->argument(0).toString();
    QString message = context->argument(0).toString()
        .replace("\\", "\\\\")
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("'", "\\'");
    engine->evaluate("Script.print('" + message + "')");
    return QScriptValue();
}

QScriptValue avatarDataToScriptValue(QScriptEngine* engine, AvatarData* const &in) {
    return engine->newQObject(in);
}

void avatarDataFromScriptValue(const QScriptValue &object, AvatarData* &out) {
    out = qobject_cast<AvatarData*>(object.toQObject());
}

QScriptValue inputControllerToScriptValue(QScriptEngine *engine, AbstractInputController* const &in) {
    return engine->newQObject(in);
}

void inputControllerFromScriptValue(const QScriptValue &object, AbstractInputController* &out) {
    out = qobject_cast<AbstractInputController*>(object.toQObject());
}

ScriptEngine::ScriptEngine(const QString& scriptContents, const QString& fileNameString,
                           AbstractControllerScriptingInterface* controllerScriptingInterface) :

    _scriptContents(scriptContents),
    _isFinished(false),
    _isRunning(false),
    _isInitialized(false),
    _isAvatar(false),
    _avatarIdentityTimer(NULL),
    _avatarBillboardTimer(NULL),
    _timerFunctionMap(),
    _isListeningToAudioStream(false),
    _avatarSound(NULL),
    _numAvatarSoundSentBytes(0),
    _controllerScriptingInterface(controllerScriptingInterface),
    _avatarData(NULL),
    _scriptName(),
    _fileNameString(fileNameString),
    _quatLibrary(),
    _vec3Library(),
    _uuidLibrary(),
    _isUserLoaded(false),
    _arrayBufferClass(new ArrayBufferClass(this))
{
}

void ScriptEngine::setIsAvatar(bool isAvatar) {
    _isAvatar = isAvatar;

    if (_isAvatar && !_avatarIdentityTimer) {
        // set up the avatar timers
        _avatarIdentityTimer = new QTimer(this);
        _avatarBillboardTimer = new QTimer(this);

        // connect our slot
        connect(_avatarIdentityTimer, &QTimer::timeout, this, &ScriptEngine::sendAvatarIdentityPacket);
        connect(_avatarBillboardTimer, &QTimer::timeout, this, &ScriptEngine::sendAvatarBillboardPacket);

        // start the timers
        _avatarIdentityTimer->start(AVATAR_IDENTITY_PACKET_SEND_INTERVAL_MSECS);
        _avatarBillboardTimer->start(AVATAR_BILLBOARD_PACKET_SEND_INTERVAL_MSECS);
    }

    if (!_isAvatar) {
        delete _avatarIdentityTimer;
        _avatarIdentityTimer = NULL;
        delete _avatarBillboardTimer;
        _avatarBillboardTimer = NULL;
    }
}

void ScriptEngine::setAvatarData(AvatarData* avatarData, const QString& objectName) {
    _avatarData = avatarData;

    // remove the old Avatar property, if it exists
    globalObject().setProperty(objectName, QScriptValue());

    // give the script engine the new Avatar script property
    registerGlobalObject(objectName, _avatarData);
}

void ScriptEngine::setAvatarHashMap(AvatarHashMap* avatarHashMap, const QString& objectName) {
    // remove the old Avatar property, if it exists
    globalObject().setProperty(objectName, QScriptValue());

    // give the script engine the new avatar hash map
    registerGlobalObject(objectName, avatarHashMap);
}

bool ScriptEngine::setScriptContents(const QString& scriptContents, const QString& fileNameString) {
    if (_isRunning) {
        return false;
    }
    _scriptContents = scriptContents;
    _fileNameString = fileNameString;
    return true;
}

void ScriptEngine::loadURL(const QUrl& scriptURL) {
    if (_isRunning) {
        return;
    }

    _fileNameString = scriptURL.toString();
    
    QUrl url(scriptURL);
    
    // if the scheme length is one or lower, maybe they typed in a file, let's try
    const int WINDOWS_DRIVE_LETTER_SIZE = 1;
    if (url.scheme().size() <= WINDOWS_DRIVE_LETTER_SIZE) {
        url = QUrl::fromLocalFile(_fileNameString);
    }
    
    // ok, let's see if it's valid... and if so, load it
    if (url.isValid()) {
        if (url.scheme() == "file") {
            _fileNameString = url.toLocalFile();
            QFile scriptFile(_fileNameString);
            if (scriptFile.open(QFile::ReadOnly | QFile::Text)) {
                qDebug() << "ScriptEngine loading file:" << _fileNameString;
                QTextStream in(&scriptFile);
                _scriptContents = in.readAll();
                emit scriptLoaded(_fileNameString);
            } else {
                qDebug() << "ERROR Loading file:" << _fileNameString;
                emit errorLoadingScript(_fileNameString);
            }
        } else {
            QNetworkAccessManager& networkAccessManager = NetworkAccessManager::getInstance();
            QNetworkReply* reply = networkAccessManager.get(QNetworkRequest(url));
            connect(reply, &QNetworkReply::finished, this, &ScriptEngine::handleScriptDownload);
        }
    }
}

void ScriptEngine::handleScriptDownload() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    
    if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute) == 200) {
        _scriptContents = reply->readAll();
        emit scriptLoaded(_fileNameString);
    } else {
        qDebug() << "ERROR Loading file:" << reply->url().toString();
        emit errorLoadingScript(_fileNameString);
    }
    
    reply->deleteLater();
}

void ScriptEngine::init() {
    if (_isInitialized) {
        return; // only initialize once
    }
    
    _isInitialized = true;

    // register various meta-types
    registerMetaTypes(this);
    registerMIDIMetaTypes(this);
    registerEventTypes(this);
    registerMenuItemProperties(this);
    registerAnimationTypes(this);
    registerAvatarTypes(this);
    registerAudioMetaTypes(this);
    Bitstream::registerTypes(this);

    qScriptRegisterMetaType(this, EntityItemPropertiesToScriptValue, EntityItemPropertiesFromScriptValue);
    qScriptRegisterMetaType(this, EntityItemIDtoScriptValue, EntityItemIDfromScriptValue);
    qScriptRegisterMetaType(this, RayToEntityIntersectionResultToScriptValue, RayToEntityIntersectionResultFromScriptValue);
    qScriptRegisterSequenceMetaType<QVector<EntityItemID> >(this);

    qScriptRegisterSequenceMetaType<QVector<glm::vec2> >(this);
    qScriptRegisterSequenceMetaType<QVector<glm::quat> >(this);
    qScriptRegisterSequenceMetaType<QVector<QString> >(this);

    QScriptValue xmlHttpRequestConstructorValue = newFunction(XMLHttpRequestClass::constructor);
    globalObject().setProperty("XMLHttpRequest", xmlHttpRequestConstructorValue);

    QScriptValue printConstructorValue = newFunction(debugPrint);
    globalObject().setProperty("print", printConstructorValue);

    QScriptValue audioEffectOptionsConstructorValue = newFunction(AudioEffectOptions::constructor);
    globalObject().setProperty("AudioEffectOptions", audioEffectOptionsConstructorValue);
    
    qScriptRegisterMetaType(this, injectorToScriptValue, injectorFromScriptValue);
    qScriptRegisterMetaType(this, inputControllerToScriptValue, inputControllerFromScriptValue);
    qScriptRegisterMetaType(this, avatarDataToScriptValue, avatarDataFromScriptValue);
    qScriptRegisterMetaType(this, animationDetailsToScriptValue, animationDetailsFromScriptValue);

    registerGlobalObject("Script", this);
    registerGlobalObject("Audio", &AudioScriptingInterface::getInstance());
    registerGlobalObject("Controller", _controllerScriptingInterface);
    registerGlobalObject("Entities", &_entityScriptingInterface);
    registerGlobalObject("Quat", &_quatLibrary);
    registerGlobalObject("Vec3", &_vec3Library);
    registerGlobalObject("Uuid", &_uuidLibrary);
    registerGlobalObject("AnimationCache", DependencyManager::get<AnimationCache>().data());

    // constants
    globalObject().setProperty("TREE_SCALE", newVariant(QVariant(TREE_SCALE)));
    globalObject().setProperty("COLLISION_GROUP_ENVIRONMENT", newVariant(QVariant(COLLISION_GROUP_ENVIRONMENT)));
    globalObject().setProperty("COLLISION_GROUP_AVATARS", newVariant(QVariant(COLLISION_GROUP_AVATARS)));

    globalObject().setProperty("AVATAR_MOTION_OBEY_LOCAL_GRAVITY", newVariant(QVariant(AVATAR_MOTION_OBEY_LOCAL_GRAVITY)));
    globalObject().setProperty("AVATAR_MOTION_OBEY_ENVIRONMENTAL_GRAVITY", newVariant(QVariant(AVATAR_MOTION_OBEY_ENVIRONMENTAL_GRAVITY)));

}

QScriptValue ScriptEngine::registerGlobalObject(const QString& name, QObject* object) {
    if (object) {
        QScriptValue value = newQObject(object);
        globalObject().setProperty(name, value);
        return value;
    }
    return QScriptValue::NullValue;
}

void ScriptEngine::registerFunction(const QString& name, QScriptEngine::FunctionSignature fun, int numArguments) {
    QScriptValue scriptFun = newFunction(fun, numArguments);
    globalObject().setProperty(name, scriptFun);
}

void ScriptEngine::registerGetterSetter(const QString& name, QScriptEngine::FunctionSignature getter,
                                        QScriptEngine::FunctionSignature setter, QScriptValue object) {
    QScriptValue setterFunction = newFunction(setter, 1);
    QScriptValue getterFunction = newFunction(getter);

    if (!object.isNull()) {
        object.setProperty(name, setterFunction, QScriptValue::PropertySetter);
        object.setProperty(name, getterFunction, QScriptValue::PropertyGetter);
    } else {
        globalObject().setProperty(name, setterFunction, QScriptValue::PropertySetter);
        globalObject().setProperty(name, getterFunction, QScriptValue::PropertyGetter);
    }
}

void ScriptEngine::evaluate() {
    if (!_isInitialized) {
        init();
    }

    QScriptValue result = evaluate(_scriptContents);

    if (hasUncaughtException()) {
        int line = uncaughtExceptionLineNumber();
        qDebug() << "Uncaught exception at (" << _fileNameString << ") line" << line << ":" << result.toString();
        emit errorMessage("Uncaught exception at (" + _fileNameString + ") line" + QString::number(line) + ":" + result.toString());
        clearExceptions();
    }
}

QScriptValue ScriptEngine::evaluate(const QString& program, const QString& fileName, int lineNumber) {
    QScriptValue result = QScriptEngine::evaluate(program, fileName, lineNumber);
    if (hasUncaughtException()) {
        int line = uncaughtExceptionLineNumber();
        qDebug() << "Uncaught exception at (" << _fileNameString << " : " << fileName << ") line" << line << ": " << result.toString();
    }
    emit evaluationFinished(result, hasUncaughtException());
    clearExceptions();
    return result;
}

void ScriptEngine::sendAvatarIdentityPacket() {
    if (_isAvatar && _avatarData) {
        _avatarData->sendIdentityPacket();
    }
}

void ScriptEngine::sendAvatarBillboardPacket() {
    if (_isAvatar && _avatarData) {
        _avatarData->sendBillboardPacket();
    }
}

void ScriptEngine::run() {
    if (!_isInitialized) {
        init();
    }
    _isRunning = true;
    _isFinished = false;
    emit runningStateChanged();

    QScriptValue result = evaluate(_scriptContents);
    if (hasUncaughtException()) {
        int line = uncaughtExceptionLineNumber();
        qDebug() << "Uncaught exception at (" << _fileNameString << ") line" << line << ":" << result.toString();
        emit errorMessage("Uncaught exception at (" + _fileNameString + ") line" + QString::number(line) + ":" + result.toString());
        clearExceptions();
    }

    QElapsedTimer startTime;
    startTime.start();

    int thisFrame = 0;

    auto nodeList = DependencyManager::get<NodeList>();

    qint64 lastUpdate = usecTimestampNow();

    while (!_isFinished) {
        int usecToSleep = (thisFrame++ * SCRIPT_DATA_CALLBACK_USECS) - startTime.nsecsElapsed() / 1000; // nsec to usec
        if (usecToSleep > 0) {
            usleep(usecToSleep);
        }

        if (_isFinished) {
            break;
        }

        QCoreApplication::processEvents();

        if (_isFinished) {
            break;
        }

        if (_entityScriptingInterface.getEntityPacketSender()->serversExist()) {
            // release the queue of edit entity messages.
            _entityScriptingInterface.getEntityPacketSender()->releaseQueuedMessages();

            // since we're in non-threaded mode, call process so that the packets are sent
            if (!_entityScriptingInterface.getEntityPacketSender()->isThreaded()) {
                _entityScriptingInterface.getEntityPacketSender()->process();
            }
        }

        if (_isAvatar && _avatarData) {

            const int SCRIPT_AUDIO_BUFFER_SAMPLES = floor(((SCRIPT_DATA_CALLBACK_USECS * AudioConstants::SAMPLE_RATE)
                                                           / (1000 * 1000)) + 0.5);
            const int SCRIPT_AUDIO_BUFFER_BYTES = SCRIPT_AUDIO_BUFFER_SAMPLES * sizeof(int16_t);

            QByteArray avatarPacket = byteArrayWithPopulatedHeader(PacketTypeAvatarData);
            avatarPacket.append(_avatarData->toByteArray());

            nodeList->broadcastToNodes(avatarPacket, NodeSet() << NodeType::AvatarMixer);

            if (_isListeningToAudioStream || _avatarSound) {
                // if we have an avatar audio stream then send it out to our audio-mixer
                bool silentFrame = true;

                int16_t numAvailableSamples = SCRIPT_AUDIO_BUFFER_SAMPLES;
                const int16_t* nextSoundOutput = NULL;

                if (_avatarSound) {

                    const QByteArray& soundByteArray = _avatarSound->getByteArray();
                    nextSoundOutput = reinterpret_cast<const int16_t*>(soundByteArray.data()
                                                                       + _numAvatarSoundSentBytes);

                    int numAvailableBytes = (soundByteArray.size() - _numAvatarSoundSentBytes) > SCRIPT_AUDIO_BUFFER_BYTES
                        ? SCRIPT_AUDIO_BUFFER_BYTES
                        : soundByteArray.size() - _numAvatarSoundSentBytes;
                    numAvailableSamples = numAvailableBytes / sizeof(int16_t);


                    // check if the all of the _numAvatarAudioBufferSamples to be sent are silence
                    for (int i = 0; i < numAvailableSamples; ++i) {
                        if (nextSoundOutput[i] != 0) {
                            silentFrame = false;
                            break;
                        }
                    }

                    _numAvatarSoundSentBytes += numAvailableBytes;
                    if (_numAvatarSoundSentBytes == soundByteArray.size()) {
                        // we're done with this sound object - so set our pointer back to NULL
                        // and our sent bytes back to zero
                        _avatarSound = NULL;
                        _numAvatarSoundSentBytes = 0;
                    }
                }
                
                QByteArray audioPacket = byteArrayWithPopulatedHeader(silentFrame
                                                                      ? PacketTypeSilentAudioFrame
                                                                      : PacketTypeMicrophoneAudioNoEcho);

                QDataStream packetStream(&audioPacket, QIODevice::Append);

                // pack a placeholder value for sequence number for now, will be packed when destination node is known
                int numPreSequenceNumberBytes = audioPacket.size();
                packetStream << (quint16) 0;

                if (silentFrame) {
                    if (!_isListeningToAudioStream) {
                        // if we have a silent frame and we're not listening then just send nothing and break out of here
                        break;
                    }

                    // write the number of silent samples so the audio-mixer can uphold timing
                    packetStream.writeRawData(reinterpret_cast<const char*>(&SCRIPT_AUDIO_BUFFER_SAMPLES), sizeof(int16_t));

                    // use the orientation and position of this avatar for the source of this audio
                    packetStream.writeRawData(reinterpret_cast<const char*>(&_avatarData->getPosition()), sizeof(glm::vec3));
                    glm::quat headOrientation = _avatarData->getHeadOrientation();
                    packetStream.writeRawData(reinterpret_cast<const char*>(&headOrientation), sizeof(glm::quat));

                } else if (nextSoundOutput) {
                    // assume scripted avatar audio is mono and set channel flag to zero
                    packetStream << (quint8)0;

                    // use the orientation and position of this avatar for the source of this audio
                    packetStream.writeRawData(reinterpret_cast<const char*>(&_avatarData->getPosition()), sizeof(glm::vec3));
                    glm::quat headOrientation = _avatarData->getHeadOrientation();
                    packetStream.writeRawData(reinterpret_cast<const char*>(&headOrientation), sizeof(glm::quat));

                    // write the raw audio data
                    packetStream.writeRawData(reinterpret_cast<const char*>(nextSoundOutput), numAvailableSamples * sizeof(int16_t));
                }
                
                // write audio packet to AudioMixer nodes
                auto nodeList = DependencyManager::get<NodeList>();
                nodeList->eachNode([this, &nodeList, &audioPacket, &numPreSequenceNumberBytes](const SharedNodePointer& node){
                    // only send to nodes of type AudioMixer
                    if (node->getType() == NodeType::AudioMixer) {
                        // pack sequence number
                        quint16 sequence = _outgoingScriptAudioSequenceNumbers[node->getUUID()]++;
                        memcpy(audioPacket.data() + numPreSequenceNumberBytes, &sequence, sizeof(quint16));
                        
                        // send audio packet
                        nodeList->writeDatagram(audioPacket, node);
                    }
                });
            }
        }

        qint64 now = usecTimestampNow();
        float deltaTime = (float) (now - lastUpdate) / (float) USECS_PER_SECOND;

        if (hasUncaughtException()) {
            int line = uncaughtExceptionLineNumber();
            qDebug() << "Uncaught exception at (" << _fileNameString << ") line" << line << ":" << uncaughtException().toString();
            emit errorMessage("Uncaught exception at (" + _fileNameString + ") line" + QString::number(line) + ":" + uncaughtException().toString());
            clearExceptions();
        }

        emit update(deltaTime);
        lastUpdate = now;
    }
    emit scriptEnding();

    // kill the avatar identity timer
    delete _avatarIdentityTimer;

    if (_entityScriptingInterface.getEntityPacketSender()->serversExist()) {
        // release the queue of edit entity messages.
        _entityScriptingInterface.getEntityPacketSender()->releaseQueuedMessages();

        // since we're in non-threaded mode, call process so that the packets are sent
        if (!_entityScriptingInterface.getEntityPacketSender()->isThreaded()) {
            _entityScriptingInterface.getEntityPacketSender()->process();
        }
    }

    // If we were on a thread, then wait till it's done
    if (thread()) {
        thread()->quit();
    }

    emit finished(_fileNameString);

    _isRunning = false;
    emit runningStateChanged();
}

void ScriptEngine::stop() {
    _isFinished = true;
    emit runningStateChanged();
}

void ScriptEngine::timerFired() {
    QTimer* callingTimer = reinterpret_cast<QTimer*>(sender());
    QScriptValue timerFunction = _timerFunctionMap.value(callingTimer);
    
    if (!callingTimer->isActive()) {
        // this timer is done, we can kill it
        _timerFunctionMap.remove(callingTimer);
        delete callingTimer;
    }
    
    // call the associated JS function, if it exists
    if (timerFunction.isValid()) {
        timerFunction.call();
    }
}

QObject* ScriptEngine::setupTimerWithInterval(const QScriptValue& function, int intervalMS, bool isSingleShot) {
    // create the timer, add it to the map, and start it
    QTimer* newTimer = new QTimer(this);
    newTimer->setSingleShot(isSingleShot);

    connect(newTimer, &QTimer::timeout, this, &ScriptEngine::timerFired);

    // make sure the timer stops when the script does
    connect(this, &ScriptEngine::scriptEnding, newTimer, &QTimer::stop);

    _timerFunctionMap.insert(newTimer, function);

    newTimer->start(intervalMS);
    return newTimer;
}

QObject* ScriptEngine::setInterval(const QScriptValue& function, int intervalMS) {
    return setupTimerWithInterval(function, intervalMS, false);
}

QObject* ScriptEngine::setTimeout(const QScriptValue& function, int timeoutMS) {
    return setupTimerWithInterval(function, timeoutMS, true);
}

void ScriptEngine::stopTimer(QTimer *timer) {
    if (_timerFunctionMap.contains(timer)) {
        timer->stop();
        _timerFunctionMap.remove(timer);
        delete timer;
    }
}

QUrl ScriptEngine::resolvePath(const QString& include) const {
    QUrl url(include);
    // first lets check to see if it's already a full URL
    if (!url.scheme().isEmpty()) {
        return url;
    }

    // we apparently weren't a fully qualified url, so, let's assume we're relative
    // to the original URL of our script
    QUrl parentURL;
    if (_parentURL.isEmpty()) {
        parentURL = QUrl(_fileNameString);
    } else {
        parentURL = QUrl(_parentURL);
    }
    // if the parent URL's scheme is empty, then this is probably a local file...
    if (parentURL.scheme().isEmpty()) {
        parentURL = QUrl::fromLocalFile(_fileNameString);
    }

    // at this point we should have a legitimate fully qualified URL for our parent
    url = parentURL.resolved(url);
    return url;
}

void ScriptEngine::print(const QString& message) {
    emit printedMessage(message);
}

/**
 * If a callback is specified, the included files will be loaded asynchronously and the callback will be called
 * when all of the files have finished loading.
 * If no callback is specified, the included files will be loaded synchronously and will block execution until
 * all of the files have finished loading.
 */
void ScriptEngine::include(const QStringList& includeFiles, QScriptValue callback) {
    QList<QUrl> urls;
    for (QString file : includeFiles) {
        urls.append(resolvePath(file));
    }

    BatchLoader* loader = new BatchLoader(urls);
    
    auto evaluateScripts = [=](const QMap<QUrl, QString>& data) {
        for (QUrl url : urls) {
            QString contents = data[url];
            if (contents.isNull()) {
                qDebug() << "Error loading file: " << url;
            } else {
                QScriptValue result = evaluate(contents, url.toString());
            }
        }

        if (callback.isFunction()) {
            QScriptValue(callback).call();
        }

        loader->deleteLater();
    };

    connect(loader, &BatchLoader::finished, this, evaluateScripts);

    // If we are destroyed before the loader completes, make sure to clean it up
    connect(this, &QObject::destroyed, loader, &QObject::deleteLater);

    loader->start();

    if (!callback.isFunction() && !loader->isFinished()) {
        QEventLoop loop;
        QObject::connect(loader, &BatchLoader::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
}

void ScriptEngine::include(const QString& includeFile, QScriptValue callback) {
    QStringList urls;
    urls.append(includeFile);
    include(urls, callback);
}

void ScriptEngine::load(const QString& loadFile) {
    QUrl url = resolvePath(loadFile);
    emit loadScript(url.toString(), false);
}

void ScriptEngine::nodeKilled(SharedNodePointer node) {
    _outgoingScriptAudioSequenceNumbers.remove(node->getUUID());
}
