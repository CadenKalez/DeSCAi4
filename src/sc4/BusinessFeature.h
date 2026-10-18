// Two small game objects.
//  * cSC4BusinessUnit (12 functions, 0x10018357a..0x100183d84, class id 0x4a232da8, iids 0x6a233069 /
//    0xe4fda3d4): a group of lots owned by one business (+8 serializable view, +0x10 property holder, +0x38 the
//    member list, +0x48 a counter that Write stores but nothing here maintains). Add / Remove keep each lot's
//    back pointer (lot vslot 0xd0) in step; Remove also clears lot vslot 0x1e0.
//  * cSC4FeatureManager (14 functions, 0x1006bf7ca..0x1006bfca0, iid 0x0b562673): the set of enabled game features
//    (expansion-pack switches). StaticInit reads the "Features" section of up to 32 feature ini resources
//    (type 0x8a5971c5, group 0, instance 0x2b563701 + n); an entry's key is the feature id and a value of "off"
//    removes it. Bit n of the installed-packs mask records that resource n exists.
#pragma once
#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gz/cIGZSerializable.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4BusinessUnit = 0x4a232da8;
constexpr std::uint32_t kGZIID_cISC4BusinessUnit = 0x6a233069;
constexpr std::uint32_t kGZIID_cISC4Lot = 0xe818fced;

class cSC4BusinessUnit;

// What a business unit needs of its member lots.
class IBusinessLot : public cIGZUnknown {
public:
    virtual void SetBusinessUnit(cSC4BusinessUnit* unit) = 0;     // vslot 0xd0
    virtual void ClearOwnerLink() = 0;                            // vslot 0x1e0 (called with 0 on removal)
};

class cISC4BusinessUnit : public cIGZUnknown {
public:
    virtual void Add(IBusinessLot* lot) = 0;
    virtual void Remove(IBusinessLot* lot) = 0;
    virtual std::list<IBusinessLot*>& GetMembers() = 0;
};

class cSC4BusinessUnit final : public cISC4BusinessUnit, public cIGZSerializable {
public:
    ~cSC4BusinessUnit();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4BusinessUnit; }
    void Add(IBusinessLot* lot) override;
    void Remove(IBusinessLot* lot) override;
    std::list<IBusinessLot*>& GetMembers() override { return members_; }
    cSCPropertyHolder& Properties() { return properties_; }
    void SetCounter(std::uint32_t counter) { counter_ = counter; }

private:
    cSCPropertyHolder properties_;           // +0x10
    std::list<IBusinessLot*> members_;       // +0x38 (each entry holds a reference)
    std::uint32_t counter_ = 0;              // +0x48
    std::uint32_t refCount_ = 0;
};

constexpr std::uint32_t kGZIID_cISC4FeatureManager = 0x0b562673;

class IFeatureHost {
public:
    virtual ~IFeatureHost() = default;
    // The "Features" entries of feature resource `index` (0..31); false when the resource does not exist.
    virtual bool LoadFeatureEntries(unsigned index, std::vector<std::pair<std::string, std::string>>& entries) = 0;
};
void SetFeatureHost(IFeatureHost* host);

class cISC4FeatureManager : public cIGZUnknown {
public:
    virtual bool IsFeatureEnabled(std::uint32_t feature) const = 0;
    virtual bool IsExpansionPackInstalled(int pack) const = 0;
    virtual int GetCurrentExpansionPack() const = 0;
};

class cSC4FeatureManager final : public cISC4FeatureManager {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init();
    bool Shutdown();
    bool IsFeatureEnabled(std::uint32_t feature) const override;
    bool IsExpansionPackInstalled(int pack) const override;
    int GetCurrentExpansionPack() const override;

    static bool StaticInit();
    static bool StaticShutdown(void* = nullptr);
    static void EnableFeatures(const std::string& key, const std::string& value);

private:
    bool initialised_ = false;   // +8
    std::uint32_t refCount_ = 0; // +0xc
};

}  // namespace sc4
