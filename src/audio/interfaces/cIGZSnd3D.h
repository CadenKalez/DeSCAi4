// cIGZSnd3D — positional sound control. Vtable slots 3..16 (+0x18..+0x80), from the cIGZSnd3D
// sub-vtable of cHitChannel / cHitTrackPlayer.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSnd;

class cIGZSnd3D : public cIGZUnknown {
public:
    virtual cIGZSnd* AsIGZSnd() = 0;                                                       // [3]
    virtual bool GetPosition(float* x, float* y, float* z) const = 0;                      // [4]
    virtual bool SetPosition(float x, float y, float z) = 0;                               // [5]
    virtual bool GetVelocity(float* x, float* y, float* z) const = 0;                      // [6]
    virtual bool SetVelocity(float x, float y, float z) = 0;                               // [7]
    virtual bool GetAll(float* px, float* py, float* pz, float* vx, float* vy, float* vz) const = 0;  // [8]
    virtual bool SetAll(float px, float py, float pz, float vx, float vy, float vz) = 0;   // [9]
    virtual void SetPower(std::uint32_t power) = 0;                                        // [10]
    virtual bool GetMinimumDistance(float* distance) const = 0;                            // [11]
    virtual bool SetMinimumDistance(float distance) = 0;                                   // [12]
    virtual bool GetMaximumDistance(float* distance) const = 0;                            // [13]
    virtual bool SetMaximumDistance(float distance) = 0;                                   // [14]
    virtual bool SetHeadRelative(bool headRelative) = 0;                                   // [15]
    virtual bool GetHeadRelative() const = 0;                                              // [16]
};

constexpr std::uint32_t kGZIID_cIGZSnd3D = 0x306077d2;
