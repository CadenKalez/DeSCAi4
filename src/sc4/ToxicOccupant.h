// cSC4ToxicOccupant — one cell of toxic waste left by the toxic-cloud disaster (48 functions,
// 0x1006c78ee..0x1006c8094). Class id 0xabf98cb5, occupant type 0xabf98cb4, version 1 record: occupant data,
// u32 value68, u32 hosed amount. Layout: +8 cSC4Occupant, +0x68 value68, +0x6c hosed, +0x70/+0x74 cell,
// +0x78 effect active, +0x80 visual effect, +0x88 world position. While initialized it shows the
// "toxic_disaster_fx" visual effect at its position and joins occupant group 0x5006.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4ToxicOccupant = 0xabf98cb5;
constexpr std::uint32_t kGZIID_cISC4ToxicOccupant = 0xabf98cb3;
constexpr std::uint32_t kOccupantTypeToxic = 0xabf98cb4;
constexpr std::uint32_t kOccupantGroupToxic = 0x5006;

// A running visual effect (reference counted).
class IToxicEffect {
public:
    virtual ~IToxicEffect() = default;
    virtual void Release() = 0;                              // vslot 0x10
    virtual void Start(int flags) = 0;                       // vslot 0x18
    virtual void Stop(int flags) = 0;                        // vslot 0x20
    virtual void SetTransform(const cS3DTransform& t) = 0;   // vslot 0x38
};

class IToxicHost {
public:
    virtual ~IToxicHost() = default;
    virtual IToxicEffect* CreateEffect(const char* name) = 0;           // effects manager vslot 0x30
    virtual void CellToWorld(int cellX, int cellZ, float& x, float& z) = 0;   // city vslot 0x308
    virtual float TerrainHeight(float x, float z) = 0;                  // terrain vslot 0x60
};
IToxicHost* ToxicHost();
void SetToxicHost(IToxicHost* host);

class cSC4ToxicOccupant : public cSC4Occupant {
public:
    cSC4ToxicOccupant() { type_ = kOccupantTypeToxic; }
    ~cSC4ToxicOccupant() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    bool Init() override;
    bool Shutdown() override;
    void GetPosition(float* xyz) override;
    void GetBoundingBox(float* minXyz, float* maxXyz) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4ToxicOccupant; }

    void UpdateVisualEffectTransform();
    void SetHosed(std::int32_t delta);    // adds `delta`, clamped at zero
    void SetCell(int cellX, int cellZ);
    cSC4Occupant* AsOccupant() { return this; }
    std::uint32_t GetHosed() const { return hosed_; }
    std::uint32_t GetAmount() const { return value68_; }           // +0x68 (set by the toxic disaster)
    void SetAmount(std::uint32_t amount) { value68_ = amount; }
    int GetCellX() const { return cellX_; }
    int GetCellZ() const { return cellZ_; }
    bool IsActive() const { return active_; }

private:
    std::uint32_t value68_ = 0;      // +0x68
    std::uint32_t hosed_ = 0;        // +0x6c
    std::int32_t cellX_ = 0, cellZ_ = 0;   // +0x70, +0x74
    bool active_ = false;            // +0x78
    IToxicEffect* effect_ = nullptr; // +0x80
    float position_[3] = {};         // +0x88
};

}  // namespace sc4
