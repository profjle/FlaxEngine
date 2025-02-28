// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

#pragma once

#if AUDIO_API_XAUDIO2

#include "../AudioBackend.h"

/// <summary>
/// The XAudio2 audio backend.
/// </summary>
class AudioBackendXAudio2 : public AudioBackend
{
public:

    // [AudioBackend]
    void Listener_OnAdd(AudioListener* listener) override;
    void Listener_OnRemove(AudioListener* listener) override;
    void Listener_VelocityChanged(AudioListener* listener) override;
    void Listener_TransformChanged(AudioListener* listener) override;
    void Source_OnAdd(AudioSource* source) override;
    void Source_OnRemove(AudioSource* source) override;
    void Source_VelocityChanged(AudioSource* source) override;
    void Source_TransformChanged(AudioSource* source) override;
    void Source_VolumeChanged(AudioSource* source) override;
    void Source_PitchChanged(AudioSource* source) override;
    void Source_IsLoopingChanged(AudioSource* source) override;
    void Source_MinDistanceChanged(AudioSource* source) override;
    void Source_AttenuationChanged(AudioSource* source) override;
    void Source_ClipLoaded(AudioSource* source) override;
    void Source_Cleanup(AudioSource* source) override;
    void Source_Play(AudioSource* source) override;
    void Source_Pause(AudioSource* source) override;
    void Source_Stop(AudioSource* source) override;
    void Source_SetCurrentBufferTime(AudioSource* source, float value) override;
    float Source_GetCurrentBufferTime(const AudioSource* source) override;
    void Source_SetNonStreamingBuffer(AudioSource* source) override;
    void Source_GetProcessedBuffersCount(AudioSource* source, int32& processedBuffersCount) override;
    void Source_GetQueuedBuffersCount(AudioSource* source, int32& queuedBuffersCount) override;
    void Source_QueueBuffer(AudioSource* source, uint32 bufferId) override;
    void Source_DequeueProcessedBuffers(AudioSource* source) override;
    void Buffer_Create(uint32& bufferId) override;
    void Buffer_Delete(uint32& bufferId) override;
    void Buffer_Write(uint32 bufferId, byte* samples, const AudioDataInfo& info) override;
    const Char* Base_Name() override;
    void Base_OnActiveDeviceChanged() override;
    void Base_SetDopplerFactor(float value) override;
    void Base_SetVolume(float value) override;
    bool Base_Init() override;
    void Base_Update() override;
    void Base_Dispose() override;
};

#endif
