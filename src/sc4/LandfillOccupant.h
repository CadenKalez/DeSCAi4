// cSC4LandfillOccupant — one 16 m landfill tile (49 functions, 0x1000b2ee2..0x1000b356e). Class id 0x8a22bebe,
// occupant type 0x6a11bd2b, version 3 record: occupant data, u32 tile id, s32 orientation, float3 position,
// u8 "empty tile" flag. Layout: +8 cSC4Occupant, +0x68 position, +0x74 bbox min, +0x80 bbox max, +0x8c tile id,
// +0x90 orientation, +0x94 empty flag. The bounding box is always position + (-8, 0, -8) .. (+8, +16, +8).
// Changing the tile id also rewrites the instance of the occupant's default exemplar key.
#pragma once
#include <cstdint>

#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4LandfillOccupant = 0x8a22bebe;
constexpr std::uint32_t kGZIID_cISC4LandfillOccupant = 0x2a15f262;
constexpr std::uint32_t kOccupantTypeLandfill = 0x6a11bd2b;

class cSC4LandfillOccupant : public cSC4Occupant {
public:
    cSC4LandfillOccupant(std::uint32_t tileId = 0, std::int32_t orientation = 0);

    // Unlike most occupants it answers only its own interfaces (no fall-through to the base).
    bool QueryInterface(std::uint32_t iid, void** out) override;
    void GetPosition(float* xyz) override;
    void SetPosition(const float* xyz) override;
    void GetBoundingBox(float* minXyz, float* maxXyz) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4LandfillOccupant; }

    std::uint32_t SetTileID(std::uint32_t tileId);   // returns the previous id
    std::uint32_t GetTileID() const { return tileId_; }
    bool SetOrientation(std::int32_t orientation) { orientation_ = orientation; return true; }
    std::int32_t GetOrientation() const { return orientation_; }
    cSC4Occupant* AsOccupant() { return this; }
    bool IsEmptyLandfillTile() const { return empty_; }
    void SetEmptyLandfillTile(bool empty) { empty_ = empty; }

private:
    void UpdateBounds();

    float position_[3] = {};      // +0x68
    float boundsMin_[3] = {};     // +0x74
    float boundsMax_[3] = {};     // +0x80
    std::uint32_t tileId_;        // +0x8c
    std::int32_t orientation_;    // +0x90
    bool empty_ = false;          // +0x94
};

}  // namespace sc4
