// cIHitTrackPlayer — the primary interface of cHitTrackPlayer: runs a Hit track (a small
// bytecode program). Slots 3..19 (+0x18..+0xa0); interface id 0xbb1be367.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSnd3D.h"
#include "audio/interfaces/cIGZSndMultipleSources.h"
#include "audio/interfaces/cIGZSndRegisterSet.h"
#include "audio/interfaces/cIHitList.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cIGZString.h"

class cIHitTrackPlayer : public cIGZUnknown {
public:
    virtual cIGZSnd* AsIGZSnd() = 0;                                              // [3]
    virtual cIGZSnd3D* AsIGZSnd3D() = 0;                                          // [4]
    virtual cIGZSndMultipleSources* AsIGZSndMultipleSources() = 0;                // [5]
    virtual cIGZSndRegisterSet* AsIGZSndRegisterSet() = 0;                        // [6]
    virtual bool SetTrackDef(cIGZPropertySet* trackDef) = 0;                      // [7]
    virtual bool GetTrackDef(std::uint32_t iid, void** out) = 0;                  // [8] the property set that defines the track
    virtual bool SetHitList(cIHitList* list) = 0;                                 // [9]
    virtual bool GetHitList(std::uint32_t iid, void** out) = 0;                   // [10]
    virtual float CompletionTime() = 0;                                           // [11]
    virtual bool Reset() = 0;                                                     // [12]
    virtual void Update(std::int32_t elapsedMs) = 0;                              // [13]
    virtual bool UpdateArgs(std::int32_t, std::int32_t, std::int32_t, std::int32_t) = 0;  // [14] returns false
    virtual bool PlayPause() = 0;                                                 // [15]
    virtual void SetErrorCondition() = 0;                                         // [16]
    virtual bool GetErrorCondition() = 0;                                         // [17]
    virtual void Step() = 0;                                                      // [18]
    virtual bool CurrentPlayPosition(const std::uint8_t** position) = 0;          // [19]
};

constexpr std::uint32_t kGZIID_cIHitTrackPlayer = 0xbb1be367;
