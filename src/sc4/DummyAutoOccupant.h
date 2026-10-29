// cSC4DummyAutoOccupant — a bare occupant that only carries a position (+0x60) and one occupant group (+0x6c); the automata code
// uses it as a stand-in target. Recovered: GetPosition, SetPosition, GetBoundingBox (a zero-size box at the position),
// IsOccupantGroup (exact match with the one group), destructor.
#pragma once
#include "sc4/Occupant.h"

namespace sc4 {

class cSC4DummyAutoOccupant : public cSC4Occupant {
public:
    cSC4DummyAutoOccupant() = default;
    void GetPosition(float* xyz) override { xyz[0] = position_[0]; xyz[1] = position_[1]; xyz[2] = position_[2]; }   // @ 0x1005f9e8e
    void SetPosition(const float* xyz) override { position_[0] = xyz[0]; position_[1] = xyz[1]; position_[2] = xyz[2]; }   // @ 0x1005f9ea6
    void GetBoundingBox(float* minXyz, float* maxXyz) override {               // @ 0x1005f9ebe
        for (int i = 0; i < 3; ++i) minXyz[i] = maxXyz[i] = position_[i];
    }
    bool IsOccupantGroup(std::uint32_t group) const override { return group_ == group; }   // @ 0x1005f9ef4
    void SetGroup(std::uint32_t group) { group_ = group; }

private:
    float position_[3] = {0, 0, 0};   // +0x60
    std::uint32_t group_ = 0;         // +0x6c
};

// cSC4DummyOccupant (0x100575a1a..): the same position-only occupant without the group; IsOccupantGroup is the base class's.
class cSC4DummyOccupant : public cSC4Occupant {
public:
    void GetPosition(float* xyz) override { xyz[0] = position_[0]; xyz[1] = position_[1]; xyz[2] = position_[2]; }   // @ 0x100575a1a
    void SetPosition(const float* xyz) override { position_[0] = xyz[0]; position_[1] = xyz[1]; position_[2] = xyz[2]; }   // @ 0x100575a32
    void GetBoundingBox(float* minXyz, float* maxXyz) override {               // @ 0x100575a4a
        for (int i = 0; i < 3; ++i) minXyz[i] = maxXyz[i] = position_[i];
    }

private:
    float position_[3] = {0, 0, 0};   // +0x60
};

}  // namespace sc4
