// cIExternalSoundSystem — optional hook that lets an external tool (the music editor) play cells
// instead of the game's sound manager. NOTHING in the shipped binary implements it; the Freshness
// classes only call two of its slots. The remaining slots are not recovered.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cFreshCell;

class cIExternalSoundSystem : public cIGZUnknown {
public:
    virtual void UnrecoveredSlot3() = 0;                                   // [3] +0x18 not referenced
    // [4] +0x20 called with the cell's fade-out time as a float (see cFreshCellPlayer::Update).
    virtual void StopCell(float fadeOutTime, cFreshCell* cell) = 0;
    virtual void UnrecoveredSlot5() = 0;                                   // [5] +0x28
    virtual void UnrecoveredSlot6() = 0;                                   // [6] +0x30
    virtual void UnrecoveredSlot7() = 0;                                   // [7] +0x38
    virtual void UnrecoveredSlot8() = 0;                                   // [8] +0x40
    virtual bool IsCellFinished(cFreshCell* cell) = 0;                     // [9] +0x48
};
