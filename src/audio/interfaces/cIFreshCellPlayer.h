// cIFreshCellPlayer — plays one cell of a score. Slots 3..22 (+0x18..+0xb0); the class adds the
// destructors at slots 23-24. Names verbatim from the vtable.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIFreshCell.h"

class cIExternalSoundSystem;

class cIFreshCellPlayer : public cIGZUnknown {
public:
    virtual cIFreshCell* GetCell() = 0;                                              // [3]
    virtual void SetCell(cIFreshCell* cell) = 0;                                     // [4]
    virtual bool Start() = 0;                                                        // [5]
    virtual void Update() = 0;                                                       // [6] called once per beat
    virtual bool Stop() = 0;                                                         // [7] begins the fade-out
    virtual void Kill() = 0;                                                         // [8] immediate
    virtual void SetVol(std::int32_t volume) = 0;                                    // [9]
    virtual bool IsPlaying() = 0;                                                    // [10] state == playing
    virtual bool IsBusy() = 0;                                                       // [11] state != idle
    virtual bool IsDead() = 0;                                                       // [12]
    virtual void SetDead(bool dead) = 0;                                             // [13]
    virtual void AutoPan() = 0;                                                      // [14]
    virtual void SetHeadRelative(bool headRelative) = 0;                             // [15]
    virtual void Set3DPosition(std::int32_t x, std::int32_t y, std::int32_t z) = 0;  // [16]
    virtual void SetMinMaxDistance(std::uint32_t minimum, std::uint32_t maximum) = 0;  // [17]
    virtual void SetExternalSoundSystemInterface(cIExternalSoundSystem* system) = 0;   // [18]
    virtual bool GetHeadRelative() = 0;                                              // [19]
    virtual std::int32_t Get3DPosX() = 0;                                            // [20]
    virtual std::int32_t Get3DPosY() = 0;                                            // [21]
    virtual std::int32_t Get3DPosZ() = 0;                                            // [22]
};

constexpr std::uint32_t kGZIID_cIFreshCellPlayer = 0x5dd9d787;
