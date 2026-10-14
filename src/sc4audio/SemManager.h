// cSC4AudioSemManager — installs the SimCity 4 emanation models into the Hit manager at startup and
// restores the engine defaults at shutdown. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (vtable 0x100aac570, 0x40-byte object, Create @ 0x1003f46b6, Init @ 0x1003f4020,
// Shutdown @ 0x1003f4484). Interface id 0xaa355871.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIHitMan.h"
#include "sc4audio/EmanationModels.h"

class cSC4AudioSemManager : public cIGZUnknown {
public:
    static cSC4AudioSemManager* Create() { return new cSC4AudioSemManager(); }  // @ 0x1003f46b6

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 1, 0xaa355871  @ 0x1003f4694
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;                              // destroys at count == 1
    virtual ~cSC4AudioSemManager() = default;

    // Fetches the Hit manager, remembers its default sems, creates and initialises the three SC4
    // models and registers them (the effects model additionally under its own id). False when
    // the manager, the defaults or a model's Init is missing.
    bool Init(cIHitMan* hitMan);          // @ 0x1003f4020
    bool Shutdown();                      // @ 0x1003f4484

    const cSC4AudioEmanationModel2D* model2D() const { return model2D_.get(); }

private:
    std::int32_t refCount_ = 0;                       // +8
    cRZAutoRefCount<cSC4AudioEmanationModel2D> model2D_;           // +0x10
    cRZAutoRefCount<cIHitSem> default2D_;             // +0x18
    cRZAutoRefCount<cSC4AudioEmanationModel3D> model3D_;           // +0x20
    cRZAutoRefCount<cIHitSem> default3D_;             // +0x28
    cRZAutoRefCount<cSC4AudioEmanationModel3DEffects> effects_;    // +0x30
    cIHitMan* hitMan_ = nullptr;                      // +0x38 (kept with a reference)
};

constexpr std::uint32_t kGZIID_cSC4AudioSemManager = 0xaa355871;
