// cSC4LocalGridHelper — the 8-byte helper every placement tool embeds to show the "local grid" effect
// under the cursor (5 named functions plus the destructor, 0x1000a98aa..0x1000a9b01). The object is
// exactly one pointer, +0: a cISC4VisualEffect* named "local_grid" that SL::spEffectsManager creates in
// Init. It is embedded at
// +0x108 in cSC4ViewInputControlPlaceLot (ctor @ 0x1003bb7c9, Init @ 0x1003bbfa9, Shutdown, SetPosition
// and ShowEffect) and in the network tool (ctor @ 0x10038deea).
//
// Decoded from the disassembly (otool -arch x86_64 -tV):
//   ctor       0x1000a98aa  *this = 0
//   dtor       0x1000a98b8  if (*this) effect->Release() vslot 0x10 (it does *not* stop the effect first)
//   Init       0x1000a98ce  if (*this != 0) return; SL::spEffectsManager vslot 0x30 ("local_grid", this)
//   Shutdown   0x1000a98f8  if (!*this) return; if (effect->IsStarted()) effect->Stop(0);
//                           *this = 0 (before the release, 0x1000a9926); effect->Release() vslot 0x10
//   SetPosition 0x1000a9940 if (!*this) return; build a cS3DTransform and call effect->SetEffectTransform
//                           vslot 0x40. change counter 2; flags 1 when the scale is exactly 1.0f else 3;
//                           identity rotation; translation = { snapped x, position.y, snapped z };
//                           snapped x/z = floor(x / cell) * cell + cell / 16 with cell = SL::spTerrain
//                           vslot 0x30 (CellSize); scale = cell * 16.0f (0x1000a9a52).
//   ShowEffect 0x1000a9aac  show: Start(0) vslot 0x18 only when !IsStarted(); hide: Stop(0) vslot 0x20
//                           only when IsStarted().
//
// Everything outside the object goes through ILocalGridHost, one method per original global: the effects
// manager (SL::spEffectsManager vslot 0x30) and the terrain cell size (SL::spTerrain vslot 0x30). The
// effect interface mirrors only the four cISC4VisualEffect slots this class calls (Release comes from
// cIGZUnknown), with their original vtable offsets in the comments (the adapter owns any extra slots).
//
// Quirks kept (asserted in scripts/tests/sc4_localgridhelper_test.cpp):
//  * Init creates the effect lazily and only once: Init after a successful call (or after a Create that
//    returned an object) never creates a second one (0x1000a98d1).
//  * Shutdown clears +0 before it releases the effect (0x1000a9926), so a re-entrant Release sees null.
//  * The destructor releases the effect *without* stopping it (0x1000a98c8); an effect that is still
//    started at destruction keeps running, unlike the Shutdown path.
//  * SetPosition snaps to the *floor* of the cell plus 1/16 of a cell, not to the cell centre, and the
//    effect scale is 16 cells (0x1000a99d1 / 0x1000a9a52).
//  * The flags byte is 1 only when the computed scale is exactly 1.0f (a NaN scale gives 3, because the
//    original compares with ucomiss + setnp/sete, 0x1000a9a5f).
#pragma once

#include "gz/StreamOps.h"     // cS3DVector3, cS3DTransform
#include "gz/cIGZUnknown.h"

namespace sc4 {

// cISC4VisualEffect as this helper calls it (the original vtable offsets are in the comments).
class ILocalGridEffect : public cIGZUnknown {
public:
    virtual void Start(int transition) = 0;                                  // vslot 0x18
    virtual void Stop(int transition) = 0;                                   // vslot 0x20
    virtual bool IsStarted() = 0;                                            // vslot 0x28
    virtual void SetEffectTransform(const cS3DTransform& transform) = 0;     // vslot 0x40
};

// SL::spEffectsManager vslot 0x30: CreateVisualEffect(name, &out), one reference on success.
class ILocalGridEffects {
public:
    virtual ~ILocalGridEffects() = default;
    virtual ILocalGridEffect* Create(const char* name) = 0;
};

// SL::spTerrain vslot 0x30: the size of one city cell in world units.
class ILocalGridTerrain {
public:
    virtual ~ILocalGridTerrain() = default;
    virtual float CellSize() = 0;
};

// Everything outside the object; one method per original global.
class ILocalGridHost {
public:
    virtual ~ILocalGridHost() = default;
    virtual ILocalGridEffects* Effects() = 0;   // SL::spEffectsManager
    virtual ILocalGridTerrain* Terrain() = 0;   // SL::spTerrain
};
ILocalGridHost* LocalGridHost();
void SetLocalGridHost(ILocalGridHost* host);

class cSC4LocalGridHelper final {
public:
    cSC4LocalGridHelper() = default;                                        // @ 0x1000a98aa
    ~cSC4LocalGridHelper();                                                 // @ 0x1000a98b8
    cSC4LocalGridHelper(const cSC4LocalGridHelper&) = delete;
    cSC4LocalGridHelper& operator=(const cSC4LocalGridHelper&) = delete;

    void Init();                                                            // @ 0x1000a98ce
    void Shutdown();                                                        // @ 0x1000a98f8
    void SetPosition(const cS3DVector3& position);                          // @ 0x1000a9940
    void ShowEffect(bool show);                                             // @ 0x1000a9aac

    // Inspection for the test / the owner.
    bool IsInitialized() const { return effect_ != nullptr; }
    ILocalGridEffect* Effect() const { return effect_; }

private:
    ILocalGridEffect* effect_ = nullptr;    // +0, the "local_grid" effect (one reference while non-null)
};

}  // namespace sc4
