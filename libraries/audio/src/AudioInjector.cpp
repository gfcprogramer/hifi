//
//  AudioInjector.cpp
//  libraries/audio/src
//
//  Created by Stephen Birarda on 1/2/2014.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtCore/QDataStream>

#include <NodeList.h>
#include <PacketHeaders.h>
#include <SharedUtil.h>
#include <UUID.h>

#include "AbstractAudioInterface.h"
#include "AudioRingBuffer.h"

#include "AudioInjector.h"

QScriptValue injectorToScriptValue(QScriptEngine* engine, AudioInjector* const& in) {
    return engine->newQObject(in);
}

void injectorFromScriptValue(const QScriptValue& object, AudioInjector*& out) {
    out = qobject_cast<AudioInjector*>(object.toQObject());
}

AudioInjector::AudioInjector(QObject* parent) :
    QObject(parent),
    _options(),
    _shouldStop(false),
    _loudness(0.0f),
    _isFinished(false),
    _currentSendPosition(0),
    _localBuffer(NULL)
{
}

AudioInjector::AudioInjector(Sound* sound, const AudioInjectorOptions& injectorOptions) :
    _audioData(sound->getByteArray()),
    _options(injectorOptions),
    _shouldStop(false),
    _loudness(0.0f),
    _isFinished(false),
    _currentSendPosition(0),
    _localBuffer(NULL)
{
}

AudioInjector::AudioInjector(const QByteArray& audioData, const AudioInjectorOptions& injectorOptions) :
    _audioData(audioData),
    _options(injectorOptions),
    _shouldStop(false),
    _loudness(0.0f),
    _isFinished(false),
    _currentSendPosition(0),
    _localBuffer(NULL)
{
    
}

AudioInjector::~AudioInjector() {
    if (_localBuffer) {
        _localBuffer->stop();
    }
}

void AudioInjector::setOptions(AudioInjectorOptions& options) {
    _options = options;
}

float AudioInjector::getLoudness() {
    return _loudness;
}

void AudioInjector::injectAudio() {
    
    // check if we need to offset the sound by some number of seconds
    if (_options.secondOffset > 0.0f) {
        
        // convert the offset into a number of bytes
        int byteOffset = (int) floorf(AudioConstants::SAMPLE_RATE * _options.secondOffset * (_options.stereo ? 2.0f : 1.0f));
        byteOffset *= sizeof(int16_t);
        
        _currentSendPosition = byteOffset;
    }
    
    if (_options.localOnly) {
        injectLocally();
    } else {
        injectToMixer();
    }
}

void AudioInjector::injectLocally() {
    bool success = false;
    if (_localAudioInterface) {
        if (_audioData.size() > 0) {
            
            _localBuffer = new AudioInjectorLocalBuffer(_audioData, this);
            _localBuffer->open(QIODevice::ReadOnly);
            _localBuffer->setShouldLoop(_options.loop);
            
            // give our current send position to the local buffer
            _localBuffer->setCurrentOffset(_currentSendPosition);
            
            QMetaObject::invokeMethod(_localAudioInterface, "outputLocalInjector",
                                      Qt::BlockingQueuedConnection,
                                      Q_RETURN_ARG(bool, success),
                                      Q_ARG(bool, _options.stereo),
                                      Q_ARG(qreal, _options.volume),
                                      Q_ARG(AudioInjector*, this));
            
            // if we're not looping and the buffer tells us it is empty then emit finished
            connect(_localBuffer, &AudioInjectorLocalBuffer::bufferEmpty, this, &AudioInjector::stop);
            
            if (!success) {
                qDebug() << "AudioInjector::injectLocally could not output locally via _localAudioInterface";
            }
        } else {
            qDebug() << "AudioInjector::injectLocally called without any data in Sound QByteArray";
        }
        
    } else {
        qDebug() << "AudioInjector::injectLocally cannot inject locally with no local audio interface present.";
    }
    
    if (!success) {
        // we never started so we are finished, call our stop method
        stop();
    }
    
}

const uchar MAX_INJECTOR_VOLUME = 0xFF;

void AudioInjector::injectToMixer() {
    if (_currentSendPosition < 0 ||
        _currentSendPosition >= _audioData.size()) {
        _currentSendPosition = 0;
    }
    
    // make sure we actually have samples downloaded to inject
    if (_audioData.size()) {
        
        // setup the packet for injected audio
        QByteArray injectAudioPacket = byteArrayWithPopulatedHeader(PacketTypeInjectAudio);
        QDataStream packetStream(&injectAudioPacket, QIODevice::Append);
        
        // pack some placeholder sequence number for now
        int numPreSequenceNumberBytes = injectAudioPacket.size();
        packetStream << (quint16)0;
        
        // pack stream identifier (a generated UUID)
        packetStream << QUuid::createUuid();
        
        // pack the stereo/mono type of the stream
        packetStream << _options.stereo;
        
        // pack the flag for loopback
        uchar loopbackFlag = (uchar) true;
        packetStream << loopbackFlag;
        
        // pack the position for injected audio
        int positionOptionOffset = injectAudioPacket.size();
        packetStream.writeRawData(reinterpret_cast<const char*>(&_options.position),
                                  sizeof(_options.position));
        
        // pack our orientation for injected audio
        int orientationOptionOffset = injectAudioPacket.size();
        packetStream.writeRawData(reinterpret_cast<const char*>(&_options.orientation),
                                  sizeof(_options.orientation));
        
        // pack zero for radius
        float radius = 0;
        packetStream << radius;
        
        // pack 255 for attenuation byte
        int volumeOptionOffset = injectAudioPacket.size();
        quint8 volume = MAX_INJECTOR_VOLUME * _options.volume;
        packetStream << volume;
        
        packetStream << _options.ignorePenumbra;
        
        QElapsedTimer timer;
        timer.start();
        int nextFrame = 0;
        
        int numPreAudioDataBytes = injectAudioPacket.size();
        bool shouldLoop = _options.loop;
        
        // loop to send off our audio in NETWORK_BUFFER_LENGTH_SAMPLES_PER_CHANNEL byte chunks
        quint16 outgoingInjectedAudioSequenceNumber = 0;
        while (_currentSendPosition < _audioData.size() && !_shouldStop) {
            
            int bytesToCopy = std::min(((_options.stereo) ? 2 : 1) * AudioConstants::NETWORK_FRAME_BYTES_PER_CHANNEL,
                                       _audioData.size() - _currentSendPosition);
            
            //  Measure the loudness of this frame
            _loudness = 0.0f;
            for (int i = 0; i < bytesToCopy; i += sizeof(int16_t)) {
                _loudness += abs(*reinterpret_cast<int16_t*>(_audioData.data() + _currentSendPosition + i)) /
                (AudioConstants::MAX_SAMPLE_VALUE / 2.0f);
            }
            _loudness /= (float)(bytesToCopy / sizeof(int16_t));

            memcpy(injectAudioPacket.data() + positionOptionOffset,
                   &_options.position,
                   sizeof(_options.position));
            memcpy(injectAudioPacket.data() + orientationOptionOffset,
                   &_options.orientation,
                   sizeof(_options.orientation));
            volume = MAX_INJECTOR_VOLUME * _options.volume;
            memcpy(injectAudioPacket.data() + volumeOptionOffset, &volume, sizeof(volume));
            
            // resize the QByteArray to the right size
            injectAudioPacket.resize(numPreAudioDataBytes + bytesToCopy);

            // pack the sequence number
            memcpy(injectAudioPacket.data() + numPreSequenceNumberBytes,
                   &outgoingInjectedAudioSequenceNumber, sizeof(quint16));
            
            // copy the next NETWORK_BUFFER_LENGTH_BYTES_PER_CHANNEL bytes to the packet
            memcpy(injectAudioPacket.data() + numPreAudioDataBytes,
                   _audioData.data() + _currentSendPosition, bytesToCopy);
            
            // grab our audio mixer from the NodeList, if it exists
            auto nodeList = DependencyManager::get<NodeList>();
            SharedNodePointer audioMixer = nodeList->soloNodeOfType(NodeType::AudioMixer);
            
            // send off this audio packet
            nodeList->writeDatagram(injectAudioPacket, audioMixer);
            outgoingInjectedAudioSequenceNumber++;
            
            _currentSendPosition += bytesToCopy;
            
            // send two packets before the first sleep so the mixer can start playback right away
            
            if (_currentSendPosition != bytesToCopy && _currentSendPosition < _audioData.size()) {
                // not the first packet and not done
                // sleep for the appropriate time
                int usecToSleep = (++nextFrame * AudioConstants::NETWORK_FRAME_USECS) - timer.nsecsElapsed() / 1000;
                
                if (usecToSleep > 0) {
                    usleep(usecToSleep);
                } 
            }

            if (shouldLoop && _currentSendPosition >= _audioData.size()) {
                _currentSendPosition = 0;
            }
        }
    }
    
    _isFinished = true;
    emit finished();
}

void AudioInjector::stop() {
    _shouldStop = true;
    
    if (_options.localOnly) {
        // we're only a local injector, so we can say we are finished right away too
        _isFinished = true;
        emit finished();
    }
}
