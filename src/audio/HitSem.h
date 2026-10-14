// cHitSem and the two built-in emanation models. Recovered from Sim City 4 Deluxe Editionsub 1.2.1.
//
// cHitSem layout (0x40): +0x00 vptr, +0x08 refcount (int), +0x10 cRZString name, +0x38 id.
// The Default models are immortal singletons: AddRef/Release do nothing and return 1.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIHitSem.h"
#include "rz/cRZString.h"

class cHitSem : public cIHitSem {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;  // matches cIGZUnknown(1) and cIHitSem
    std::uint32_t AddRef() override { return ++refCount_; }
    // Deletes the object when the count is exactly 1.
    std::uint32_t Release() override;
    std::uint32_t GetId() const override { return id_; }
    bool GetName(cIGZString& outName) const override;

protected:
    cHitSem() = default;
    virtual ~cHitSem() = default;

    std::int32_t refCount_ = 0;
    cRZString name_;
    std::uint32_t id_ = 0;
};

// Id 0x7dcded2d, name "2D default": volume = (volumeScale*volume)/1024, pan passed through.
class cHitSem2dDefault final : public cHitSem {
public:
    cHitSem2dDefault();
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;
    bool ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;
};

// Id 0x7dcded3d, name "3D default": additionally forwards position/velocity and distances.
class cHitSem3dDefault final : public cHitSem {
public:
    cHitSem3dDefault();
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;
    bool ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;
};

constexpr std::uint32_t kHitSem2dDefaultId = 0x7dcded2d;
constexpr std::uint32_t kHitSem3dDefaultId = 0x7dcded3d;
