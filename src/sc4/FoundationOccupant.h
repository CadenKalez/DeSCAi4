// cSC4FoundationOccupant — the concrete slab under a building on sloped terrain (49 functions,
// 0x10016d922..0x10016df7e). Class id 0x49c05c8f, occupant type 0x68fd0c69, version 2 record: occupant data,
// u32 foundation type, float max height, float3 position, float3 size, s8 usage, s8 orientation (0..3).
// Layout: +8 cSC4Occupant, +0x68 type, +0x6c max height, +0x70 position, +0x7c size, +0x88 usage, +0x89 orientation.
// The box hangs below the position: x/z centred on it, y from (position - size.y) up to the position.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4FoundationOccupant = 0x49c05c8f;
constexpr std::uint32_t kGZIID_cISC4FoundationOccupant = 0xc8fce101;
constexpr std::uint32_t kOccupantTypeFoundation = 0x68fd0c69;

class cSC4FoundationOccupant : public cSC4Occupant {
public:
    cSC4FoundationOccupant() { type_ = kOccupantTypeFoundation; }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    void GetPosition(float* xyz) override;
    void SetPosition(const float* xyz) override;
    void GetBoundingBox(float* minXyz, float* maxXyz) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4FoundationOccupant; }

    bool SetSize(const float* xyz);
    std::int32_t GetOrientation() const { return orientation_; }
    bool SetOrientation(std::int32_t orientation);   // clamped to 0..3
    cSC4Occupant* AsOccupant() { return this; }
    std::uint32_t GetFoundationType() const { return foundationType_; }
    void SetFoundationType(std::uint32_t type) { foundationType_ = type; }
    float GetMaxHeight() const { return maxHeight_; }
    void SetMaxHeight(float height) { maxHeight_ = height; }
    std::int32_t GetUsage() const { return usage_; }
    void SetUsage(std::int32_t usage) { usage_ = static_cast<std::int8_t>(usage); }

private:
    std::uint32_t foundationType_ = 0;   // +0x68
    float maxHeight_ = 0.0f;             // +0x6c
    cS3DVector3 position_;               // +0x70
    cS3DVector3 size_;                   // +0x7c
    std::int8_t usage_ = 0;              // +0x88
    std::int8_t orientation_ = 0;        // +0x89
};

}  // namespace sc4
