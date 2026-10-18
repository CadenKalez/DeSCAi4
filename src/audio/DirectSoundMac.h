// The DirectSound compatibility layer the game's sound code (the EA SNDSYS C library) talks to on the Mac port: IDirectSound_Mac
// hands out IDirectSoundBuffer_Mac objects that play through Core Audio queues, with IDirectSound3DBuffer_Mac /
// IDirectSound3DListener_Mac turning DirectSound's 3D parameters into a simple left/right/distance gain
// (98 functions at 0x100477c9c..0x100478b0a, 0x1007d2b74..0x1007d3950, 0x1007d3393).
//
// Core Audio is behind IAudioQueueApi so the module is testable and the port can supply its own device. Status codes are the
// original's: 0 success, 0x80000002 no free audio queue, 0x80000003 bad argument, 0x80000004 unknown interface.
//
// How it works. IDirectSound_Mac is a singleton (creating a second one releases the first) owning 256 buffer slots and a bank
// of 32 audio queues; a queue handed to a buffer by Play stays in the bank forever (Stop only detaches it), so at most 32
// sounds can ever be started. Each buffer keeps the whole sound in memory (the byte store of a slot is reused by the next buffer
// created in the same slot, never cleared) and plays it in half-second chunks: the queue callback copies the next chunk, wraps
// when looping and marks the buffer finished at the end. Volume (hundredths of a dB) becomes 0.891251^dB, pan a plain ratio;
// positional sounds get pan from the listener-relative direction through a four-quadrant table and volume from distance.
//
// Quirks kept: the pan / volume / distance multipliers are stored but never used when applying volume; DuplicateSoundBuffer
// stores the caller's old out-pointer into the free slot, returns the copy without registering it and takes an extra reference
// on the source; SetCompositionActive-style calls elsewhere are unrelated. Deviations (the original corrupts memory): a buffer
// created with no free slot (index -1) gets private memory instead of writing before the slot tables, and a format with fewer
// than 8 bits per sample no longer divides by zero.
#pragma once
#include <cstdint>
#include <cstring>

#include "s3d/S3DMath.h"

constexpr std::int32_t DS_OK = 0;
constexpr std::int32_t DSERR_NOQUEUE = static_cast<std::int32_t>(0x80000002);
constexpr std::int32_t DSERR_INVALIDPARAM = static_cast<std::int32_t>(0x80000003);
constexpr std::int32_t DSERR_NOINTERFACE = static_cast<std::int32_t>(0x80000004);

struct GUID16 {
    std::uint8_t bytes[16];
    friend bool operator==(const GUID16& a, const GUID16& b) { return std::memcmp(a.bytes, b.bytes, 16) == 0; }
};
extern GUID16 IID_IDirectSound3DBuffer, IID_IDirectSound3DListener, IID_IDirectSoundBuffer8;   // set up by the application

struct tWAVEFORMATEX {   // 18 bytes
    std::uint16_t formatTag, channels;
    std::uint32_t samplesPerSec, avgBytesPerSec;
    std::uint16_t blockAlign, bitsPerSample, cbSize;
};
struct _DSBUFFERDESC {
    std::uint32_t size, flags, bufferBytes, reserved;
    tWAVEFORMATEX* format;
};
struct _DSBCAPS { std::uint32_t size, flags, bufferBytes, unlockTransferRate, playCpuOverhead; };
struct _D3DVECTOR { float x, y, z; };
struct _DS3DLISTENER {   // 0x40 bytes
    std::uint32_t size;
    _D3DVECTOR position, velocity, orientFront, orientTop;
    float distanceFactor, rolloffFactor, dopplerFactor;
};
struct _DS3DBUFFER {     // 0x40 bytes
    std::uint32_t size;
    _D3DVECTOR position, velocity;
    std::uint32_t insideConeAngle, outsideConeAngle;
    _D3DVECTOR coneOrientation;
    std::int32_t coneOutsideVolume;
    float minDistance, maxDistance;
    std::uint32_t mode;
};
struct _DSCAPS {         // 0x60 bytes; only the fields GetCaps fills are named
    std::uint32_t size, flags, minSecondarySampleRate, maxSecondarySampleRate, primaryBuffers, maxHwMixingAllBuffers,
        maxHwMixingStaticBuffers, maxHwMixingStreamingBuffers, freeHwMixingAllBuffers, freeHwMixingStaticBuffers,
        freeHwMixingStreamingBuffers, unlockTransferRateHwBuffers, playCpuOverheadSwBuffers, reserved[10];
};

struct AudioStreamBasicDescription {
    double sampleRate = 0;                    // +0
    std::uint32_t formatID = 0;               // +8  'lpcm'
    std::uint32_t formatFlags = 0;            // +0xc
    std::uint32_t bytesPerPacket = 0;         // +0x10
    std::uint32_t framesPerPacket = 0;        // +0x14
    std::uint32_t bytesPerFrame = 0;          // +0x18
    std::uint32_t channelsPerFrame = 0;       // +0x1c
    std::uint32_t bitsPerChannel = 0;         // +0x20
    std::uint32_t reserved = 0;               // +0x24
};
struct AudioQueueBuffer {
    std::uint32_t capacity = 0;
    void* data = nullptr;                     // +8
    std::uint32_t size = 0;                   // +0x10 bytes queued
};

class IDirectSoundBuffer_Mac;

// What the module needs from Core Audio (AudioQueueNewOutput / Start / AllocateBuffer / Enqueue / SetParameter / Dispose).
class IAudioQueueApi {
public:
    virtual ~IAudioQueueApi() = default;
    // Creates an output queue whose buffer callback is AudioBufferCallback(userData, ...); null on failure.
    virtual void* NewOutput(const AudioStreamBasicDescription& format, IDirectSoundBuffer_Mac* userData) = 0;
    virtual void EnableTimePitchBypass(void* queue) = 0;                       // AudioQueueSetProperty(queue, 0x715f7470, 1)
    virtual bool Start(void* queue) = 0;
    virtual bool AllocateBuffer(void* queue, std::uint32_t bytes, AudioQueueBuffer** out) = 0;
    virtual bool Enqueue(void* queue, AudioQueueBuffer* buffer) = 0;
    virtual void SetParameter(void* queue, std::uint32_t parameter, float value) = 0;   // 1 = volume, 0xd = pan
    virtual void Dispose(void* queue) = 0;
};
IAudioQueueApi* GetAudioQueueApi();
void SetAudioQueueApi(IAudioQueueApi* api);

// IUnknown_Mac (0x1007b7300..): a plain counter; reaching zero destroys the object.
class IUnknown_Mac {
public:
    IUnknown_Mac() = default;
    virtual ~IUnknown_Mac() = default;
    virtual std::int32_t AddRef() { return ++refCount_; }                        // @ 0x1007b732a
    virtual std::int32_t Release() {                                             // @ 0x1007b7338
        const std::int32_t n = --refCount_;
        if (n == 0) delete this;
        return n;
    }
private:
    std::int32_t refCount_ = 0;                                                  // +8
};

class IDirectSound3DBuffer_Mac;
class IDirectSound3DListener_Mac;

class IDirectSound_Mac : public IUnknown_Mac {
public:
    IDirectSound_Mac();                                                           // @ 0x1004777ec
    ~IDirectSound_Mac() override;                                                 // @ 0x1004778ac

    std::int32_t QueryInterface(const GUID16&, void** out);                      // @ 0x100477998
    std::int32_t CreateSoundBuffer(const _DSBUFFERDESC* desc, IDirectSoundBuffer_Mac** out, void* outer);   // @ 0x1004779bc
    std::int32_t GetCaps(_DSCAPS* caps);                                         // @ 0x100477a76
    std::int32_t DuplicateSoundBuffer(IDirectSoundBuffer_Mac* source, IDirectSoundBuffer_Mac** out);   // @ 0x100477aee
    std::int32_t SetCooperativeLevel(void*, std::uint32_t) { return 0; }         // @ 0x100477bdc
    std::int32_t Compact() { return 0; }                                         // @ 0x100477be4
    std::int32_t GetSpeakerConfig(std::uint32_t*) { return 0; }                  // @ 0x100477bec
    std::int32_t SetSpeakerConfig(std::uint32_t) { return 0; }                   // @ 0x100477bf4
    std::int32_t Initialize(const GUID16*) { return 0; }                         // @ 0x100477bfc

    // Hands out one of the 32 queues (null when all are taken or cannot be created).
    static void* MacAudioQueueBankAllocate(const AudioStreamBasicDescription& format, IDirectSoundBuffer_Mac* userData);   // @ 0x100477c06
    IDirectSound3DListener_Mac* MacGet3DListener();                              // @ 0x100477da0
    static int MacGetSoundBufferMaxCount() { return 0x100; }                     // @ 0x100477df4
    static IDirectSoundBuffer_Mac* MacGetSoundBuffer(int index);                 // @ 0x100477e00 (the original reads it off an instance)

    static IDirectSound_Mac* Instance();                                         // s_IDirectSound_Mac
    static void ResetGlobalsForTest();

private:
    IDirectSound3DListener_Mac* listener_ = nullptr;                             // +0x18
};

class IDirectSoundBuffer_Mac : public IUnknown_Mac {
public:
    enum Kind : std::int32_t { kNone = 0, kPrimary = 1, kStatic = 2, kStreaming = 3 };

    IDirectSoundBuffer_Mac(int slot, std::int32_t bufferBytes, std::uint32_t flags, const tWAVEFORMATEX* format);   // @ 0x100477e14
    IDirectSoundBuffer_Mac(int slot, const IDirectSoundBuffer_Mac& source);       // @ 0x100478044 (duplicate)
    ~IDirectSoundBuffer_Mac() override;                                           // @ 0x100478212

    std::int32_t QueryInterface(const GUID16& iid, void** out);                  // @ 0x100478352
    void MacCreate3DBuffer();                                                    // @ 0x100478400
    IDirectSound3DBuffer_Mac* MacGet3DBuffer() { return buffer3D_; }             // @ 0x100478464
    std::int32_t GetCaps(_DSBCAPS* caps);                                        // @ 0x100478480
    std::int32_t Initialize(void*, const _DSBUFFERDESC*) { return 0; }           // @ 0x1004784a8
    std::int32_t SetFormat(const tWAVEFORMATEX* format);                         // @ 0x1004784b0
    std::int32_t GetFormat(tWAVEFORMATEX* format, std::uint32_t size, std::uint32_t* written);   // @ 0x1004784d8
    std::int32_t Lock(std::uint32_t offset, std::uint32_t bytes, void** ptr1, std::uint32_t* size1, void** ptr2,
                      std::uint32_t* size2, std::uint32_t flags);                 // @ 0x100478510
    std::int32_t Unlock(void*, std::uint32_t, void*, std::uint32_t);              // @ 0x100478666
    std::int32_t SetCurrentPosition(std::uint32_t position);                      // @ 0x100478674
    std::int32_t GetCurrentPosition(std::uint32_t* play, std::uint32_t* write);   // @ 0x1004787a8
    std::int32_t Play(std::uint32_t reserved, std::uint32_t priority, std::uint32_t flags);   // @ 0x1004787f0
    std::int32_t Stop();                                                          // @ 0x100478876
    std::int32_t SetFrequency(std::uint32_t) { return 0; }                        // @ 0x1004788a8
    std::int32_t SetPan(std::int32_t pan);                                        // @ 0x1004788b0
    std::int32_t SetVolume(std::int32_t volume);                                  // @ 0x1004789f0
    std::int32_t GetStatus(std::uint32_t* status);                                // @ 0x100478a06
    std::int32_t Restore() { return 0; }                                          // @ 0x100478a52
    std::int32_t SetFX(std::uint32_t, void*, std::uint32_t*) { return 0; }        // @ 0x100478a5a
    std::int32_t AcquireResources(std::uint32_t, std::uint32_t, std::uint32_t*) { return 0; }   // @ 0x100478a62
    std::int32_t GetObjectInPath(const GUID16&, std::uint32_t, const GUID16&, void**) { return 0; }   // @ 0x100478a6a
    std::int32_t GetVolume(std::int32_t* volume) { *volume = volume_; return 0; }   // @ 0x100478aee
    std::int32_t GetPan(std::int32_t* pan) { *pan = pan_; return 0; }              // @ 0x100478afc
    std::int32_t GetFrequency(std::uint32_t* rate) { *rate = currentRate_; return 0; }   // @ 0x100478b0a

    IDirectSound_Mac* MacGetDirectSound() { return owner_; }                       // @ 0x100478a72
    void MacSetPanVolumeMultipliers(float left, float right, float distance);      // @ 0x100478a80 (stored, then volume / pan re-applied)
    void MacInitSoundInfo();                                                       // @ 0x100477fcc
    void MacAdjustSampleRate();                                                    // @ 0x1004785f4
    void MacPlaySndBuffer(bool loop);                                              // @ 0x1004786e8
    void MacApplyVolumeAndPan();                                                   // @ 0x10047893e
    void SetOwner(IDirectSound_Mac* owner) { owner_ = owner; }
    void MacSetBuffer3DNull() { buffer3D_ = nullptr; }

    // State the audio callback and tests look at.
    Kind kind() const { return kind_; }
    bool playing() const { return playing_; }
    bool looping() const { return loop_; }
    void* queue() const { return queue_; }
    std::uint32_t chunkBytes() const { return chunkBytes_; }
    std::uint32_t size() const { return static_cast<std::uint32_t>(size_); }
    const AudioStreamBasicDescription& streamFormat() const { return asbd_; }
    float leftMultiplier() const { return leftMultiplier_; }
    float rightMultiplier() const { return rightMultiplier_; }
    float distanceMultiplier() const { return distanceMultiplier_; }

    friend void AudioBufferCallback(IDirectSoundBuffer_Mac* buffer, void* queue, AudioQueueBuffer* audioBuffer);

private:
    void AllocateBytes(int slot, std::uint32_t bytes);

    bool playing_ = false;                      // +0x14
    void* queue_ = nullptr;                     // +0x18
    Kind kind_ = kNone;                         // +0x20
    std::int32_t volume_ = 0;                   // +0x24 (hundredths of a dB)
    std::uint32_t currentRate_ = 0;             // +0x28
    std::uint32_t originalRate_ = 0;            // +0x2c
    std::int32_t pan_ = 0;                      // +0x30
    float leftMultiplier_ = 1.0f;               // +0x34
    float rightMultiplier_ = 1.0f;              // +0x38
    float distanceMultiplier_ = 1.0f;           // +0x3c
    bool loop_ = false;                         // +0x40
    std::uint8_t* data_ = nullptr;              // +0x48
    std::int32_t size_ = 0;                     // +0x50
    AudioStreamBasicDescription asbd_;          // +0x58
    tWAVEFORMATEX format_{};                    // +0x80
    std::uint64_t playPosition_ = 0;            // +0x98
    std::uint64_t nextFill_ = 0;                // +0xa0
    std::uint32_t chunkBytes_ = 0;              // +0xa8
    std::uint32_t pendingChunk_ = 0;            // +0xac
    bool wrapped_ = false;                      // +0xb0
    bool stopped_ = false;                      // +0xb1
    IDirectSound_Mac* owner_ = nullptr;         // +0xb8
    IDirectSound3DBuffer_Mac* buffer3D_ = nullptr;   // +0xc0
};

// The queue's refill callback (AudioBufferCallback @ 0x100477c9c).
void AudioBufferCallback(IDirectSoundBuffer_Mac* buffer, void* queue, AudioQueueBuffer* audioBuffer);
extern float gAmbientVolumeLimit;   // _gAmbientVolumeLimit: volume scale for non-positional sounds, 1.0 unless 3D sounds exist

class IDirectSound3DBuffer_Mac {
public:
    IDirectSound3DBuffer_Mac();                                                   // @ 0x1007d2b74
    std::int32_t QueryInterface(const GUID16&, void** out);                      // @ 0x1007d2bd4
    std::int64_t AddRef() { return ++refCount_; }                                // @ 0x1007d2be6
    std::int64_t Release();                                                      // @ 0x1007d2bf8
    std::int32_t GetAllParameters(_DS3DBUFFER* out);                             // @ 0x1007d2c30
    std::int32_t GetConeAngles(std::uint32_t* inside, std::uint32_t* outside);    // @ 0x1007d2c86
    std::int32_t GetConeOrientation(_D3DVECTOR* out);                            // @ 0x1007d2ca8
    std::int32_t GetConeOutsideVolume(std::int32_t* out);                        // @ 0x1007d2cca
    std::int32_t GetMaxDistance(float* out);                                     // @ 0x1007d2ce4
    std::int32_t GetMinDistance(float* out);                                     // @ 0x1007d2cfe
    std::int32_t GetMode(std::uint32_t* out);                                    // @ 0x1007d2d18
    std::int32_t GetPosition(_D3DVECTOR* out);                                   // @ 0x1007d2d32
    std::int32_t GetVelocity(_D3DVECTOR* out);                                   // @ 0x1007d2d54
    std::int32_t SetAllParameters(const _DS3DBUFFER* in, std::uint32_t mode);     // @ 0x1007d2d76
    std::int32_t SetConeAngles(std::uint32_t inside, std::uint32_t outside, std::uint32_t mode);   // @ 0x1007d2e1c
    std::int32_t SetConeOrientation(float x, float y, float z, std::uint32_t mode);   // @ 0x1007d2e4a
    std::int32_t SetConeOutsideVolume(std::int32_t volume, std::uint32_t mode);   // @ 0x1007d2e82
    std::int32_t SetMaxDistance(float distance, std::uint32_t mode);             // @ 0x1007d2eae
    std::int32_t SetMinDistance(float distance, std::uint32_t mode);             // @ 0x1007d2edc
    std::int32_t SetMode(std::uint32_t value, std::uint32_t mode);               // @ 0x1007d2f0a
    std::int32_t SetPosition(float x, float y, float z, std::uint32_t mode);      // @ 0x1007d2f36
    std::int32_t SetVelocity(float x, float y, float z, std::uint32_t mode);      // @ 0x1007d2f6e
    void MacUpdateListener();                                                    // @ 0x1007d2df0
    void MacApplyPosition();                                                     // @ 0x1007d2fa6
    void MacSetBuffer(IDirectSoundBuffer_Mac* buffer) { buffer_ = buffer; }      // @ 0x1007d350c
    bool positionDirty() const { return dirtyPosition_; }
    void MarkPositionDirty() { dirtyPosition_ = true; }

private:
    std::int64_t refCount_ = 1;                  // +8
    _DS3DBUFFER params_{};                       // +0x10
    IDirectSoundBuffer_Mac* buffer_ = nullptr;   // +0x50
    bool dirtyPosition_ = false;                 // +0x58 (position, min / max distance)
    bool dirtyVelocity_ = false;                 // +0x59
    bool dirtyConeAngles_ = false;               // +0x5a
    bool dirtyConeOrientation_ = false;          // +0x5b
    bool dirtyConeVolume_ = false;               // +0x5c
    bool dirtyMode_ = false;                     // +0x5d
};

class IDirectSound3DListener_Mac {
public:
    IDirectSound3DListener_Mac();                                                 // @ 0x1007d3518
    std::int32_t QueryInterface(const GUID16&, void** out);                      // @ 0x1007d3570
    std::int32_t AddRef() { return ++refCount_; }                                // @ 0x1007d3582
    std::int32_t Release();                                                      // @ 0x1007d3590
    std::int32_t GetAllParameters(_DS3DLISTENER* out);                           // @ 0x1007d35a8
    std::int32_t GetDistanceFactor(float* out);                                  // @ 0x1007d35fe
    std::int32_t GetDopplerFactor(float* out);                                   // @ 0x1007d3618
    std::int32_t GetOrientation(_D3DVECTOR* front, _D3DVECTOR* top);              // @ 0x1007d3632
    std::int32_t GetPosition(_D3DVECTOR* out);                                   // @ 0x1007d3664
    std::int32_t GetRolloffFactor(float* out);                                   // @ 0x1007d3686
    std::int32_t GetVelocity(_D3DVECTOR* out);                                   // @ 0x1007d36a0
    std::int32_t SetAllParameters(const _DS3DLISTENER* in, std::uint32_t mode);   // @ 0x1007d36c2
    std::int32_t SetDistanceFactor(float value, std::uint32_t mode);             // @ 0x1007d3744
    std::int32_t SetDopplerFactor(float value, std::uint32_t mode);              // @ 0x1007d3768
    std::int32_t SetOrientation(float fx, float fy, float fz, float tx, float ty, float tz, std::uint32_t mode);   // @ 0x1007d378c
    std::int32_t SetPosition(float x, float y, float z, std::uint32_t mode);      // @ 0x1007d38f2
    std::int32_t SetRolloffFactor(float value, std::uint32_t mode);              // @ 0x1007d392c
    std::int32_t SetVelocity(float x, float y, float z, std::uint32_t mode);      // @ 0x1007d3950
    std::int32_t CommitDeferredSettings();                                       // @ 0x1007d397e
    void MacCreateListenerMatrix();                                              // @ 0x1007d37d6
    void MacCommitDeferredBufferSettings();                                      // @ 0x1007d34a4
    void MacProcess3DBuffers();                                                  // @ 0x1007d398e
    void MacSetDirectSound(IDirectSound_Mac* ds) { owner_ = ds; }                // @ 0x1007d39ec
    const matrix44& Matrix() const { return matrix_; }

private:
    std::int32_t refCount_ = 1;           // +8
    _DS3DLISTENER params_{};              // +0xc
    IDirectSound_Mac* owner_ = nullptr;   // +0x50
    matrix44 matrix_{};                   // +0x58
    bool dirtyDistance_ = false;          // +0x98
    bool dirtyDoppler_ = false;           // +0x99
    bool dirtyMatrix_ = false;            // +0x9a
    bool dirtyRolloff_ = false;           // +0x9b
    bool dirtyVelocity_ = false;          // +0x9c
};

// The angle of a direction in the horizontal plane, from +X counter-clockwise through Z: atan(z / x) shifted into [0, 2*pi).
// 0 when x or z is zero. @ 0x1007d3393
float GetXZAngle(cS3DVector3 direction);
