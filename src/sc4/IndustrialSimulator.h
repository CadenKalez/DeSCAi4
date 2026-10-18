// cSC4IndustrialSimulator (21 functions, 0x10021544a..0x1002156fa, class id 0xa990c022, iid 0x477a9e7d): the
// industrial simulator is an empty shell in this build - it only tracks "initialised", writes / reads a version
// 1 header and ignores every message. Layout: +0x18 initialised, +0x1c refcount.
#pragma once
#include <cstdint>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4IndustrialSimulator = 0xa990c022;
constexpr std::uint32_t kGZIID_cISC4IndustrialSimulator = 0x477a9e7d;

class cSC4IndustrialSimulator final : public cIGZSerializable, public cIGZMessageTarget2 {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init();
    bool Shutdown();
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4IndustrialSimulator; }
    bool DoMessage(cIGZMessage2* message) override;
    bool IsInitialised() const { return initialised_; }

private:
    bool initialised_ = false;
    std::uint32_t refCount_ = 0;
};

}  // namespace sc4
