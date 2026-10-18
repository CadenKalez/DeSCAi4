// Two tiny UI-side helper objects.
//  * cSC4LotConfigurationFilter (8 functions, 0x1006c303a..0x1006c30bc, iid 0x68f0113a): the pass-everything lot
//    configuration filter (+8 refcount); subclasses override IsLotConfigurationIncluded.
//  * cSC4CtabFilter (9 functions, 0x1003762aa..0x1003763fc, iid 0x22e85d8e): a window message filter that hands
//    Return / Tab key characters (message 4, char '\r' or '\t') to its window; +0x10 vector, +0x30 window.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/WinInterfaces.h"

namespace sc4 {

class cISC4LotConfiguration;

constexpr std::uint32_t kGZIID_cISC4LotConfigurationFilter = 0x68f0113a;
constexpr std::uint32_t kGZIID_cSC4CtabFilter = 0x22e85d8e;

class cISC4LotConfigurationFilter : public cIGZUnknown {
public:
    virtual bool IsLotConfigurationIncluded(cISC4LotConfiguration* configuration) = 0;
};

class cSC4LotConfigurationFilter : public cISC4LotConfigurationFilter {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool IsLotConfigurationIncluded(cISC4LotConfiguration*) override { return true; }

protected:
    std::uint32_t refCount_ = 0;
};

class cSC4CtabFilter final : public cIGZWinMessageFilter {
public:
    explicit cSC4CtabFilter(cIGZWin* window);
    ~cSC4CtabFilter();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoMessage(cIGZWin* window, cGZMessage& message) override;

private:
    std::uint32_t refCount_ = 0;
    std::vector<std::uint32_t> unused_;   // +0x10, freed by the destructor
    cIGZWin* window_ = nullptr;           // +0x30
};

}  // namespace sc4
