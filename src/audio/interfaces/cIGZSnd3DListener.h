// cIGZSnd3DListener / cIGZSnd3DListenerEx — the listener of the 3D sound system (cGZSnd3DListener,
// vtable 0x100ad9708). cIGZSnd3DListener is the primary base (slots 3..10, byte offsets
// +0x18..+0x50); cIGZSnd3DListenerEx is the secondary base at +8 (its own slots 3..12).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSnd3DListenerEx;

class cIGZSnd3DListener : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                       // [3]  +0x18
    virtual bool Shutdown() = 0;                                                   // [4]  +0x20
    virtual bool CommitDeferredSettings() = 0;                                     // [5]  +0x28
    virtual bool GetPosition(float* x, float* y, float* z) const = 0;              // [6]  +0x30
    virtual bool SetPosition(float x, float y, float z) = 0;                       // [7]  +0x38
    virtual bool GetOrientation(float* fx, float* fy, float* fz, float* ux, float* uy, float* uz) const = 0;   // [8] +0x40
    virtual bool SetOrientation(float fx, float fy, float fz, float ux, float uy, float uz) = 0;               // [9] +0x48 front, up
    virtual cIGZSnd3DListenerEx* AsIGZSnd3DListenerEx() = 0;                       // [10] +0x50
};

class cIGZSnd3DListenerEx : public cIGZUnknown {
public:
    virtual bool GetVelocity(float* x, float* y, float* z) const = 0;              // [3]  +0x18
    virtual bool SetVelocity(float x, float y, float z) = 0;                       // [4]  +0x20
    virtual bool GetAll(float*, float*, float*, float*, float*, float*, float*, float*, float*, float*, float*, float*) const = 0;   // [5] +0x28
    virtual bool SetAll(float, float, float, float, float, float, float, float, float, float, float, float) = 0;                      // [6] +0x30
    virtual bool GetDopplerFactor(float* factor) const = 0;                        // [7]  +0x38
    virtual bool SetDopplerFactor(float factor) = 0;                               // [8]  +0x40
    virtual bool GetRolloffFactor(float* factor) const = 0;                        // [9]  +0x48
    virtual bool SetRolloffFactor(float factor) = 0;                               // [10] +0x50
    virtual bool GetDistanceFactor(float* factor) const = 0;                       // [11] +0x58
    virtual bool SetDistanceFactor(float factor) = 0;                              // [12] +0x60
};
