#include "audio/HitManAccess.h"
#include "audio/HitRegisters.h"
#include "audio/HitSem.h"
#include "audio/HitTrackDataBlock.h"

#include <cassert>
#include <cstdio>
#include <map>

namespace {
struct FakeRegs final : cIGZSndRegisterSet {
    std::map<std::int32_t, std::int32_t> v;
    std::map<std::int32_t, bool> suppressed;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool SetRegisterValue(std::int32_t id, std::int32_t value, bool s) override { v[id] = value; suppressed[id] = s; return true; }
    std::int32_t GetRegisterValue(std::int32_t id) const override { auto it = v.find(id); return it == v.end() ? 0 : it->second; }
    void ClearRegisterValue(std::int32_t id, bool) override { v.erase(id); }
    bool HasRegisterValue(std::int32_t id) const override { return v.count(id) != 0; }
    bool SetRegisterFloat(std::uint32_t, float, bool) override { return false; }
    float GetRegisterFloat(std::uint32_t) const override { return 0; }
};
struct FakeHM final : RZ::cHitManRegisterView {
    std::int32_t duckThreshold = 0;
    std::int32_t GetRegisterValue(std::int32_t id) const override {
        return id == static_cast<std::int32_t>(nsHitReg::kPropDuckPriority) ? duckThreshold : 0;
    }
    std::int32_t Random(std::int32_t, std::int32_t) override { return 0; }
};
FakeHM g_hm;
}  // namespace
namespace RZ { cHitManRegisterView* HM() { return &g_hm; } }

int main() {
    // register id table
    using namespace nsHitReg;
    assert(GetPropertyIdFromLegacyRegisterId(0x15) == kPropVolume && GetPropertyIdFromLegacyRegisterId(0x16) == kPropPan);
    assert(GetPropertyIdFromLegacyRegisterId(0x20) == kPropPositionX && GetPropertyIdFromLegacyRegisterId(0x42) == kPropVelocityZ);
    assert(GetPropertyIdFromLegacyRegisterId(0x0d) == 0 && GetPropertyIdFromLegacyRegisterId(0x50) == 0);

    // 2D model
    cHitSem2dDefault sem2d;
    assert(sem2d.GetId() == 0x7dcded2d && sem2d.AddRef() == 1 && sem2d.Release() == 1);
    cRZString name; assert(sem2d.GetName(name) && name.str() == "2D default");
    FakeRegs src, dst;
    src.v[0x16] = -300; src.v[0x15] = 800; src.v[0x14] = 512;     // 512*800/1024 = 400
    assert(sem2d.UpdateVolPan(&src, &dst));
    assert(dst.v[0x16] == -300 && dst.v[0x15] == 400 && dst.suppressed[0x16] && !dst.suppressed[0x15]);
    g_hm.duckThreshold = 5; src.v[static_cast<std::int32_t>(kPropDuckPriority)] = 3;   // ducked: /2048
    sem2d.UpdateVolPan(&src, &dst); assert(dst.v[0x15] == 200);
    src.v[static_cast<std::int32_t>(kPropDuckPriority)] = 5;                           // not ducked
    sem2d.UpdateVolPan(&src, &dst); assert(dst.v[0x15] == 400);
    src.v[0x14] = -512;                                                                // truncation toward zero
    g_hm.duckThreshold = 0; sem2d.UpdateVolPan(&src, &dst); assert(dst.v[0x15] == -400);
    FakeRegs cfg_src, cfg_dst;
    for (int id : {0x13, 0x19, 0x1a, 0x1b, 0x23, 0x24, 0x25, 0x26, 0x33}) cfg_src.v[id] = id * 3;
    assert(sem2d.ConfigureChannel(&cfg_src, &cfg_dst) && cfg_dst.v.size() == 9 && cfg_dst.v[0x33] == 0x33 * 3);

    // 3D model
    cHitSem3dDefault sem3d;
    assert(sem3d.GetId() == 0x7dcded3d);
    FakeRegs s3, d3; s3.v[0x14] = 1024; s3.v[0x15] = 100; s3.v[0x20] = 7; s3.v[0x41] = 9;
    assert(!sem3d.UpdateVolPan(&s3, &s3));                       // same object: no work
    assert(sem3d.UpdateVolPan(&s3, &d3));
    assert(d3.v[0x20] == 7 && d3.v[0x41] == 9 && d3.v[0x1e] == 4 && d3.v[0x1f] == 2000 && d3.v[0x15] == 100);
    s3.v[0x38] = 10; sem3d.UpdateVolPan(&s3, &d3); assert(d3.v[0x1e] == 10 && d3.v[0x1f] == 5000);
    s3.v[0x1e] = 2; s3.v[0x1f] = 77; sem3d.UpdateVolPan(&s3, &d3); assert(d3.v[0x1e] == 2 && d3.v[0x1f] == 77);
    FakeRegs c3; assert(sem3d.ConfigureChannel(&cfg_src, &c3) && c3.v[0x1c] == 1);

    // track data block
    auto* blk = new cHitTrackDataBlock();
    const std::uint8_t bytes[4] = {1, 2, 3, 4};
    void* out = nullptr;
    assert(blk->QueryInterface(kGZIID_cIHitTrackDataBlock, &out) && out == blk);
    assert(blk->SetDataBlock(bytes, 4) && blk->DataStartAddress() == bytes && blk->DataSize() == 4);
    std::uint32_t off = 99;
    assert(!blk->GetEntryPointOffset(0xAB, off) && off == 99);
    assert(blk->AddEntryPoint(0xAB, 12) && blk->GetEntryPointOffset(0xAB, off) && off == 12);
    blk->AddEntryPoint(0xAB, 40); blk->GetEntryPointOffset(0xAB, off); assert(off == 40);
    blk->SetFilename(cRZString("sounds/hit.dat"), "/Games/SC4");
    cRZString fn; blk->GetFilename(fn); assert(fn.str() == "/GAMES/SC4/SOUNDS/HIT.DAT");
    blk->SetFilename(cRZString("/abs/file.dat"), "/Games/SC4");
    blk->GetFilename(fn); assert(fn.str() == "/ABS/FILE.DAT");
    assert(blk->AddRef() == 2);    // QueryInterface already took one
    assert(blk->Release() == 1);
    assert(blk->Release() == 0);   // last reference: deleted
    std::puts("audio hit foundation ok");
}
