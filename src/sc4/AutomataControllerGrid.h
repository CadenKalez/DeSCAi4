// cSC4ControllerGrid, tControllerQInfo and tControllerTime::Normalize — the spatial index and queue bookkeeping of the automata
// controller manager (cSC4ControllerGrid 7 functions at 0x1000f4322..0x1000f8452, tControllerQInfo 4 functions at 0x1000f6436..
// 0x1000f9144, tControllerTime::Normalize @ 0x1000f5d7c).
//
// The grid cuts the city into square "tracts" of 128 m (8 cells): Init sizes it from the city (cells / 8 per side, the shift is
// log2 of that, left at the last tested value when it is not a power of two). Layout: +0 vector<list<controller*>> (one list per
// tract, index (z << shift) + x), +0x18 width, +0x19 height, +0x1a shift (bytes), +0x1c city width (m), +0x20 city depth (m).
// A controller keeps the rect of tracts it occupies in its TractRect (4 bytes x0 z0 x1 z1).
// Quirks kept: GetGridBounds(position, radius) clamps both axes against the city WIDTH (the original asks vslot 0x2c8 twice);
// AddController skips tracts that already list the controller, MoveController appends to entered tracts without that check.
// tControllerQInfo is the queue of automata waiting at an occupant (usually a lot): the queued automata (counted), the point where
// they line up, the direction away from the lot edge and whether a road was found next to the lot.
#pragma once
#include <cstdint>
#include <list>
#include <vector>

#include "gz/StreamOps.h"
#include "sc4/AutomataController.h"
#include "sc4/AutomataGenerator.h"

namespace sc4 {

// tControllerTime::Normalize @ 0x1000f5d7c: folds the hours into [0, 24) moving whole days, a NaN time becomes 0, and the day number
// is reduced to 0..6 (floor modulo). operator+= is ControllerTime::operator+= (AutomataGenerator.h).
void NormalizeControllerTime(ControllerTime& time);

struct TractRect { std::uint8_t x0, z0, x1, z1; };

class cSC4ControllerGrid {
public:
    bool Init();                                                         // @ 0x1000f4322 (needs AutomataControllerHost)
    void AddController(cSC4AutomataController* controller);              // @ 0x1000f47ca
    void RemoveController(cSC4AutomataController* controller);           // @ 0x1000f5f7c
    void MoveController(cSC4AutomataController* controller);             // @ 0x1000f64f6
    void GetGridBounds(const float position[3], float radius, TractRect& out) const;   // @ 0x1000f6e10
    void GetGridBounds(const cSC4AutomataController* controller, TractRect& out) const;   // @ 0x1000f8452
    std::list<cSC4AutomataController*>& GetTractByWorldPos(float x, float z);   // @ 0x1000f74a2
    std::list<cSC4AutomataController*>& Tract(int x, int z) { return cells_[(static_cast<std::size_t>(z) << shift_) + static_cast<std::size_t>(x)]; }
    void Clear() { cells_.clear(); }                                     // vector::clear in cSC4AutomataControllerManager::ClearControllers (the grid has no tracts until Init)
    int Width() const { return width_; }
    int Height() const { return height_; }
    int Shift() const { return shift_; }
    std::size_t CellCount() const { return cells_.size(); }

private:
    std::vector<std::list<cSC4AutomataController*>> cells_;   // +0
    std::uint8_t width_ = 0, height_ = 0, shift_ = 0;         // +0x18, +0x19, +0x1a
    float cityWidth_ = 0.0f, cityDepth_ = 0.0f;               // +0x1c, +0x20
};

class tControllerQInfo : public IControllerQueue {
public:
    void Clear();                                                // @ 0x1000f6436
    void InitQueuePoint(cISC4Occupant* occupant) override;       // @ 0x1000f7efe
    bool IsValid() override { return valid; }
    const float* QueuePoint() override { return point; }
    const float* QueueDirection() override { return direction; }
    void AddToQueue(IControllerAutomaton* automaton) override;   // @ 0x1000f9064 (tells the automaton who is ahead: vslot 0x188)
    void RemoveFromQueue(IControllerAutomaton* automaton) override;   // @ 0x1000f9144

    std::list<IControllerAutomaton*> queue;   // +0x00 (counted references)
    std::int32_t savedCount = 0;              // +0x18 (set by Clear and by the stream reader)
    float point[3] = {0, 0, 0};               // +0x1c
    float direction[3] = {0, 0, 0};           // +0x28
    bool valid = false;                       // +0x34
};

}  // namespace sc4
